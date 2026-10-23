#include "model.hpp"
#include "expr.hpp"
#include "memory_budget.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <cvode/cvode.h>
#include <kinsol/kinsol.h>
#include <nvector/nvector_serial.h>
#include <sundials/sundials_context.h>
#include <sunlinsol/sunlinsol_dense.h>
#include <sunmatrix/sunmatrix_dense.h>

namespace lmbio {
namespace {

[[noreturn]] void bad(const std::string &s) {
  throw std::runtime_error("lmbio: " + s);
}
void finite(double x, const std::string &what) {
  if (!std::isfinite(x))
    bad(what + " must be finite");
}

struct Stoich {
  std::map<std::string, double> left, right;
};

std::string trim(std::string s) {
  const auto a = s.find_first_not_of(" \t\r\n");
  const auto b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<std::string> words(const std::string &s) {
  std::istringstream in(s);
  std::vector<std::string> r;
  std::string x;
  while (in >> x)
    r.push_back(x);
  return r;
}
bool mentions(const std::string &expr, const std::string &name) {
  for (size_t p = 0; (p = expr.find(name, p)) != std::string::npos; ++p) {
    const bool left =
        p == 0 || !(std::isalnum(static_cast<unsigned char>(expr[p - 1])) ||
                    expr[p - 1] == '_');
    const size_t e = p + name.size();
    const bool right =
        e == expr.size() ||
        !(std::isalnum(static_cast<unsigned char>(expr[e])) || expr[e] == '_');
    size_t next = e;
    while (next < expr.size() &&
           std::isspace(static_cast<unsigned char>(expr[next])))
      ++next;
    if (left && right && (next == expr.size() || expr[next] != '('))
      return true;
  }
  return false;
}

void add_term(std::map<std::string, double> &out, std::string term) {
  term = trim(term);
  if (term.empty())
    return;
  for (char &c : term)
    if (c == '*')
      c = ' ';
  auto w = words(term);
  if (w.size() == 1) {
    out[w[0]] += 1;
    return;
  }
  if (w.size() != 2)
    bad("reaction term must be 'species' or 'coefficient species': " + term);
  char *end = nullptr;
  double n = std::strtod(w[0].c_str(), &end);
  if (!end || *end || !std::isfinite(n) || n < 0)
    bad("invalid stoichiometric coefficient: " + w[0]);
  if (w[1].empty())
    bad("empty species in reaction");
  out[w[1]] += n;
}

Stoich parse_equation(const std::string &eq) {
  size_t p = eq.find("->");
  if (p == std::string::npos)
    p = eq.find("=>");
  if (p == std::string::npos)
    bad("reaction equation needs ->");
  std::string lhs = eq.substr(0, p),
              rhs = eq.substr(p + (eq[p] == '=' ? 2 : 2));
  Stoich s;
  auto parse_side = [&](const std::string &side,
                        std::map<std::string, double> &terms) {
    if (trim(side).empty())
      return;
    if (trim(side).back() == '+')
      bad("empty reaction term");
    std::stringstream input(side);
    std::string term;
    while (std::getline(input, term, '+')) {
      if (trim(term).empty())
        bad("empty reaction term");
      add_term(terms, term);
    }
  };
  parse_side(lhs, s.left);
  parse_side(rhs, s.right);
  if (s.left.empty() && s.right.empty())
    bad("empty reaction equation");
  return s;
}

struct Network {
  std::vector<std::string> vars;
  std::map<std::string, double> params;
  std::vector<std::unique_ptr<Expr>> assignments;
  std::vector<int> assignment_targets;
  std::vector<std::unique_ptr<Expr>> rates;
  std::vector<std::unique_ptr<Expr>> reverse_rates;
  std::vector<Stoich> stoich;
  std::vector<int> rate_rule_targets;
  std::vector<std::unique_ptr<Expr>> rate_rules;
};

struct Sun {
  SUNContext ctx = nullptr;
  Sun() {
    if (SUNContext_Create(SUN_COMM_NULL, &ctx) != 0)
      bad("SUNDIALS context creation failed");
    SUNContext_ClearErrHandlers(ctx);
  }
  ~Sun() {
    if (ctx)
      SUNContext_Free(&ctx);
  }
};

// SUNDIALS allocates with malloc, outside operator new: charge an upper estimate of what one
// solve keeps (dense Jacobian, its saved copy, pivots, about 30 state-sized vectors) before
// creating it, and release it when the solver is freed.
struct SolverResources {
  N_Vector y = nullptr, scale = nullptr;
  SUNMatrix matrix = nullptr;
  SUNLinearSolver linear = nullptr;
  void *ode = nullptr;
  void *kin = nullptr;
  std::size_t charged = 0;
  explicit SolverResources(int q) {
    const std::size_t n = static_cast<std::size_t>(q);
    charged = (2 * n * n + 32 * n) * sizeof(double) + 64 * 1024;
    membudget::charge(charged);
  }
  SolverResources(const SolverResources &) = delete;
  SolverResources &operator=(const SolverResources &) = delete;
  ~SolverResources() {
    membudget::release(charged);
    if (ode)
      CVodeFree(&ode);
    if (kin)
      KINFree(&kin);
    if (linear)
      SUNLinSolFree(linear);
    if (matrix)
      SUNMatDestroy(matrix);
    if (scale)
      N_VDestroy(scale);
    if (y)
      N_VDestroy(y);
  }
};
void validate_structure(const Model &m) {
  if (m.species_list.empty())
    bad("model has no species");
  if (m.species_list.size() + m.parameters.size() > 2048 ||
      m.reactions.size() > 10000)
    bad("model size limit exceeded");
  if (!std::isfinite(m.rtol) || !std::isfinite(m.atol) || m.rtol <= 0 ||
      m.atol <= 0)
    bad("tolerances must be finite and positive");
  std::set<std::string> ids, species, compartments, targets;
  auto add_id = [&](const std::string &id) {
    if (id.empty() ||
        !(std::isalpha(static_cast<unsigned char>(id[0])) || id[0] == '_'))
      bad("invalid identifier: " + id);
    for (char c : id)
      if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
        bad("invalid identifier: " + id);
    if (id == "t" || id == "time")
      bad("reserved time identifier: " + id);
    if (!ids.insert(id).second)
      bad("duplicate id: " + id);
  };
  for (const auto &c : m.compartments) {
    add_id(c.id);
    compartments.insert(c.id);
    if (!std::isfinite(c.size) || c.size <= 0)
      bad("compartment size must be finite and positive");
  }
  for (const auto &x : m.species_list) {
    add_id(x.id);
    species.insert(x.id);
    if (!compartments.count(x.compartment))
      bad("unknown compartment: " + x.compartment);
    if (!std::isfinite(x.initial) || x.initial < 0)
      bad("species initial value must be finite and nonnegative");
  }
  for (const auto &x : m.parameters) {
    add_id(x.first);
    finite(x.second, "parameter");
  }
  for (const auto &r : m.reactions) {
    add_id(r.id);
    if (r.rate.empty())
      bad("empty reaction rate");
    auto st = parse_equation(r.equation);
    for (const auto *side : {&st.left, &st.right})
      for (const auto &x : *side)
        if (!species.count(x.first))
          bad("unknown species in reaction: " + x.first);
    for (const auto &x : r.local_params) {
      if (x.first.empty() ||
          !(std::isalpha(static_cast<unsigned char>(x.first[0])) ||
            x.first[0] == '_'))
        bad("invalid local parameter id");
      for (char c : x.first)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
          bad("invalid local parameter id");
      if (x.first == "t" || x.first == "time")
        bad("reserved time local parameter id");
      finite(x.second, "local parameter");
    }
  }
  for (const auto &r : m.rules) {
    if (!species.count(r.target) && !m.parameters.count(r.target))
      bad("unknown rule target: " + r.target);
    if (!targets.insert(r.target).second)
      bad("duplicate rule target: " + r.target);
    if (r.expression.empty())
      bad("empty rule expression");
    for (const auto &x : m.species_list)
      if (x.id == r.target && x.constant)
        bad("rule targets constant species: " + x.id);
    for (const auto &reaction : m.reactions) {
      auto st = parse_equation(reaction.equation);
      for (const auto &x : m.species_list)
        if (x.id == r.target && !x.boundary &&
            (st.left.count(x.id) || st.right.count(x.id)))
          bad("rule target also participates in a reaction: " + x.id);
    }
  }
}
void check_grid(const Model &m, double t0, double t1, int points,
                const char *operation) {
  if (!std::isfinite(t0) || !std::isfinite(t1) || !std::isfinite(t1 - t0) ||
      t1 < t0 || points < 2 || points > 1000000)
    bad(std::string(operation) +
        " needs finite ordered times and points in [2,1000000]");
  if (static_cast<size_t>(points) *
          (1 + m.species_list.size() + m.parameters.size()) >
      10000000)
    bad("result matrix size limit exceeded");
}

int index_of(const std::vector<std::string> &v, const std::string &x) {
  auto i = std::find(v.begin(), v.end(), x);
  return i == v.end() ? -1 : int(i - v.begin());
}
int state_index(const Network &n, const std::string &x) {
  const int i = index_of(n.vars, x);
  return i < 1 ? -1 : i - 1;
}

Network compile(const Model &m) {
  validate_structure(m);
  Network n;
  n.vars.push_back("t");
  for (const auto &s : m.species_list)
    n.vars.push_back(s.id);
  for (const auto &p : m.parameters) {
    n.vars.push_back(p.first);
    n.params[p.first] = p.second;
  }
  for (const auto &c : m.compartments)
    n.params[c.id] = c.size;
  std::map<std::string, std::string> assigned;
  for (const auto &r : m.rules) {
    if (assigned.count(r.target))
      bad("duplicate rule for '" + r.target + "'");
    assigned[r.target] = r.expression;
  }
  std::set<std::string> visiting, done;
  std::vector<const Rule *> ordered;
  std::function<void(const Rule *)> visit = [&](const Rule *r) {
    if (done.count(r->target))
      return;
    if (visiting.count(r->target))
      bad("assignment rule cycle at '" + r->target + "'");
    visiting.insert(r->target);
    for (const auto &q : m.rules)
      if (!q.rate && mentions(r->expression, q.target))
        visit(&q);
    visiting.erase(r->target);
    done.insert(r->target);
    ordered.push_back(r);
  };
  for (const auto &r : m.rules)
    if (!r.rate)
      visit(&r);
  for (const Rule *r : ordered) {
    int i = state_index(n, r->target);
    if (i < 0)
      bad("rule target must be a species or parameter: " + r->target);
    n.assignment_targets.push_back(i + 1);
    n.assignments.push_back(
        std::make_unique<Expr>(r->expression, n.vars, n.params));
  }
  for (const auto &r : m.rules)
    if (r.rate) {
      int i = state_index(n, r.target);
      if (i < 0)
        bad("rate rule target unknown: " + r.target);
      if (i < int(m.species_list.size()) && m.species_list[i].constant)
        bad("rate rule cannot target constant or boundary species: " +
            r.target);
      n.rate_rule_targets.push_back(i);
      n.rate_rules.push_back(
          std::make_unique<Expr>(r.expression, n.vars, n.params));
    }
  for (const auto &r : m.reactions) {
    auto st = parse_equation(r.equation);
    n.stoich.push_back(st);
    n.rates.push_back(
        std::make_unique<Expr>(r.rate, n.vars, n.params, r.local_params));
    n.reverse_rates.push_back(
        r.reverse.empty() ? nullptr
                          : std::make_unique<Expr>(r.reverse, n.vars, n.params,
                                                   r.local_params));
  }
  return n;
}

void apply_assignments(const Network &n, std::vector<double> &v) {
  for (size_t i = 0; i < n.assignments.size(); ++i) {
    double value = n.assignments[i]->eval(v.data());
    finite(value, "assignment rule value");
    v[n.assignment_targets[i]] = value;
  }
}

std::vector<double> derivatives(const Model &m, const Network &n, double t,
                                const std::vector<double> &yin,
                                bool stochastic = false) {
  std::vector<double> v(n.vars.size());
  v[0] = t;
  std::copy(yin.begin(), yin.end(), v.begin() + 1);
  apply_assignments(n, v);
  std::vector<double> dy(m.species_list.size() + m.parameters.size(), 0.0);
  auto ci = [&](const std::string &id) {
    for (size_t i = 0; i < m.compartments.size(); ++i)
      if (m.compartments[i].id == id)
        return m.compartments[i].size;
    return 1.0;
  };
  for (size_t j = 0; j < m.reactions.size(); ++j) {
    double f = n.rates[j]->eval(v.data());
    if (!std::isfinite(f))
      bad("reaction rate is non-finite");
    double rr = n.reverse_rates[j] ? n.reverse_rates[j]->eval(v.data()) : 0;
    if (!std::isfinite(rr))
      bad("reverse reaction rate is non-finite");
    std::set<std::string> names;
    for (const auto &x : n.stoich[j].left)
      names.insert(x.first);
    for (const auto &x : n.stoich[j].right)
      names.insert(x.first);
    for (const auto &id : names) {
      int i = state_index(n, id);
      double d = n.stoich[j].right.count(id) ? n.stoich[j].right.at(id) : 0;
      d -= n.stoich[j].left.count(id) ? n.stoich[j].left.at(id) : 0;
      if (i < 0 || i >= int(m.species_list.size()))
        bad("reaction species lookup failed: " + id);
      const auto &sp = m.species_list[i];
      dy[i] += d * (f - rr) /
               (sp.substance_only || stochastic ? 1.0 : ci(sp.compartment));
    }
  }
  for (int target : n.assignment_targets) {
    const int state = target - 1;
    if (state >= 0 && state < int(m.species_list.size()))
      dy[state] = 0;
  }
  for (size_t i = 0; i < m.species_list.size(); ++i)
    if (m.species_list[i].boundary || m.species_list[i].constant)
      dy[i] = 0;
  for (size_t i = 0; i < n.rate_rules.size(); ++i)
    dy[n.rate_rule_targets[i]] = n.rate_rules[i]->eval(v.data());
  for (double value : dy)
    finite(value, "derivative");
  return dy;
}

// The callbacks run inside SUNDIALS C frames: no exception may leave them. A budget failure is
// kept (Exceeded has no heap state) and rethrown after the solver returns.
struct OdeCtx {
  const Model *m;
  const Network *n;
  int error = 0;
  std::string message;
  std::optional<membudget::Exceeded> exceeded;
};
template <class C> int callback_failed(C *c, const std::exception &e) noexcept {
  if (auto *x = dynamic_cast<const membudget::Exceeded *>(&e))
    c->exceeded = *x;
  try {
    c->message = e.what();
  } catch (...) {
    c->message.clear();
  }
  return -1;
}
template <class C> void rethrow_budget(const C &c) {
  if (c.exceeded)
    throw *c.exceeded;
}
int rhs(sunrealtype t, N_Vector y, N_Vector yd, void *u) {
  auto *c = static_cast<OdeCtx *>(u);
  try {
    std::vector<double> x(c->m->species_list.size() + c->m->parameters.size());
    for (size_t i = 0; i < x.size(); ++i)
      x[i] = NV_Ith_S(y, i);
    auto d = derivatives(*c->m, *c->n, t, x);
    for (size_t i = 0; i < d.size(); ++i)
      NV_Ith_S(yd, i) = d[i];
    return 0;
  } catch (const std::exception &e) {
    c->error = 1;
    return callback_failed(c, e);
  } catch (...) {
    c->error = 1;
    return -1;
  }
}
struct KinCtx {
  const Model *m;
  const Network *n;
  std::vector<double> base;
  std::vector<int> active;
  std::string message;
  std::optional<membudget::Exceeded> exceeded;
};
int kin_rhs(N_Vector u, N_Vector f, void *user) {
  auto *c = static_cast<KinCtx *>(user);
  try {
    auto x = c->base;
    for (size_t i = 0; i < c->active.size(); ++i)
      x[c->active[i]] = NV_Ith_S(u, i);
    auto d = derivatives(*c->m, *c->n, 0, x);
    for (size_t i = 0; i < c->active.size(); ++i)
      NV_Ith_S(f, i) = d[c->active[i]];
    return 0;
  } catch (const std::exception &e) {
    return callback_failed(c, e);
  } catch (...) {
    c->message = "unknown error";
    return -1;
  }
}

Result make_result(const Model &m, const std::vector<double> &times,
                   const std::vector<std::vector<double>> &rows) {
  Result r;
  r.rows = int(rows.size());
  r.columns.push_back("time");
  for (auto &s : m.species_list)
    r.columns.push_back(s.id);
  for (auto &p : m.parameters)
    r.columns.push_back(p.first);
  r.data.reserve(rows.size() * r.columns.size()); // exact size: no doubling slack
  for (size_t i = 0; i < rows.size(); ++i) {
    r.data.push_back(times[i]);
    r.data.insert(r.data.end(), rows[i].begin(), rows[i].end());
  }
  for (double v : r.data)
    finite(v, "result value");
  return r;
}
std::vector<double> output_state(const Network &n, double t,
                                 const std::vector<double> &state) {
  std::vector<double> v(n.vars.size());
  v[0] = t;
  std::copy(state.begin(), state.end(), v.begin() + 1);
  apply_assignments(n, v);
  return std::vector<double>(v.begin() + 1, v.end());
}

} // namespace

double Result::value(const std::string &id, int row) const {
  auto it = std::find(columns.begin(), columns.end(), id);
  if (it == columns.end())
    bad("unknown result column: " + id);
  if (row < 0 || row >= rows)
    bad("result row outside range");
  return data[size_t(row) * columns.size() + size_t(it - columns.begin())];
}
double Result::final(const std::string &id) const {
  if (!rows)
    bad("empty result");
  return value(id, rows - 1);
}
double Result::maximum(const std::string &id) const {
  if (!rows)
    bad("empty result");
  double x = -std::numeric_limits<double>::infinity();
  for (int i = 0; i < rows; ++i)
    x = std::max(x, value(id, i));
  return x;
}
double Result::time_of_max(const std::string &id) const {
  if (!rows)
    bad("empty result");
  double x = -std::numeric_limits<double>::infinity();
  int k = 0;
  for (int i = 0; i < rows; ++i)
    if (value(id, i) > x) {
      x = value(id, i);
      k = i;
    }
  return value("time", k);
}
void Result::save_csv(const std::string &path) const {
  std::ofstream f(path);
  if (!f)
    bad("cannot write " + path);
  f << std::setprecision(17);
  for (size_t i = 0; i < columns.size(); ++i)
    f << (i ? "," : "") << columns[i];
  f << '\n';
  for (int r = 0; r < rows; ++r) {
    for (size_t c = 0; c < columns.size(); ++c)
      f << (c ? "," : "") << data[size_t(r) * columns.size() + c];
    f << '\n';
  }
  f.flush();
  if (!f)
    bad("cannot write " + path);
}

Model &Model::compartment(const std::string &id, double size) {
  if (id.empty())
    bad("empty compartment id");
  finite(size, "compartment size");
  if (size <= 0)
    bad("compartment size must be positive");
  if (std::any_of(compartments.begin(), compartments.end(),
                  [&](auto &x) { return x.id == id; }))
    bad("duplicate id: " + id);
  compartments.push_back({id, size});
  return *this;
}
Model &Model::species(const std::string &id, const std::string &c,
                      double init) {
  finite(init, "species initial");
  if (std::any_of(species_list.begin(), species_list.end(),
                  [&](auto &x) { return x.id == id; }) ||
      parameters.count(id))
    bad("duplicate id: " + id);
  if (!std::any_of(compartments.begin(), compartments.end(),
                   [&](auto &x) { return x.id == c; }))
    bad("unknown compartment: " + c);
  species_list.push_back({id, c, init});
  return *this;
}
Model &Model::param(const std::string &id, double v) {
  finite(v, "parameter");
  if (id.empty() || parameters.count(id) ||
      std::any_of(species_list.begin(), species_list.end(),
                  [&](auto &x) { return x.id == id; }))
    bad("duplicate id: " + id);
  parameters[id] = v;
  return *this;
}
Model &Model::reaction(const std::string &id, const std::string &eq,
                       const std::string &rate) {
  if (id.empty() || std::any_of(reactions.begin(), reactions.end(),
                                [&](auto &x) { return x.id == id; }))
    bad("duplicate reaction id: " + id);
  if (rate.empty())
    bad("empty reaction rate");
  reactions.push_back({id, eq, rate, {}, {}});
  return *this;
}
Model &Model::reversible(const std::string &id, const std::string &rev) {
  auto it = std::find_if(reactions.begin(), reactions.end(),
                         [&](auto &x) { return x.id == id; });
  if (it == reactions.end())
    bad("unknown reaction: " + id);
  if (!it->reverse.empty())
    bad("duplicate reverse rate: " + id);
  it->reverse = rev;
  return *this;
}
Model &Model::rule(const std::string &id, const std::string &e) {
  rules.push_back({id, e, false});
  return *this;
}
Model &Model::rate_rule(const std::string &id, const std::string &e) {
  rules.push_back({id, e, true});
  return *this;
}
Model &Model::set(const std::string &id, double v) {
  finite(v, "set value");
  auto p = parameters.find(id);
  if (p != parameters.end()) {
    p->second = v;
    return *this;
  }
  for (auto &s : species_list)
    if (s.id == id) {
      s.initial = v;
      return *this;
    }
  bad("unknown symbol: " + id);
}
double Model::get(const std::string &id) const {
  auto p = parameters.find(id);
  if (p != parameters.end())
    return p->second;
  for (auto &s : species_list)
    if (s.id == id)
      return s.initial;
  bad("unknown symbol: " + id);
}
Model &Model::tol(double r, double a) {
  finite(r, "relative tolerance");
  finite(a, "absolute tolerance");
  if (r <= 0 || a <= 0)
    bad("tolerances must be positive");
  rtol = r;
  atol = a;
  return *this;
}
void Model::validate() const { (void)compile(*this); }

Result Model::simulate(double t0, double t1, int points) const {
  check_grid(*this, t0, t1, points, "simulate");
  Network n = compile(*this);
  const int q = static_cast<int>(species_list.size() + parameters.size());
  std::vector<double> initial;
  for (const auto &x : species_list)
    initial.push_back(x.initial);
  for (const auto &x : parameters)
    initial.push_back(x.second);
  std::vector<double> times(points);
  std::vector<std::vector<double>> out(points, std::vector<double>(q));
  times[0] = t0;
  out[0] = output_state(n, t0, initial);
  if (t1 == t0) {
    for (int i = 1; i < points; ++i) {
      times[i] = t0;
      out[i] = out[0];
    }
    return make_result(*this, times, out);
  }
  Sun sun;
  SolverResources resource(q);
  N_Vector y = resource.y = N_VNew_Serial(q, sun.ctx);
  if (!y)
    bad("cannot allocate CVODE state");
  for (int i = 0; i < q; ++i)
    NV_Ith_S(y, i) = initial[i];
  void *mem = resource.ode = CVodeCreate(CV_BDF, sun.ctx);
  SUNMatrix A = resource.matrix = SUNDenseMatrix(q, q, sun.ctx);
  SUNLinearSolver ls = resource.linear = SUNLinSol_Dense(y, A, sun.ctx);
  if (!mem || !A || !ls)
    bad("CVODE allocation failed");
  OdeCtx ctx{this, &n, 0, {}, {}};
  if (CVodeInit(mem, rhs, t0, y) != CV_SUCCESS ||
      CVodeSStolerances(mem, rtol, atol) != CV_SUCCESS ||
      CVodeSetUserData(mem, &ctx) != CV_SUCCESS ||
      CVodeSetLinearSolver(mem, ls, A) != CV_SUCCESS ||
      CVodeSetMaxNumSteps(mem, 500000) != CV_SUCCESS)
    bad("CVODE setup failed");
  for (int k = 1; k < points; ++k) {
    times[k] = t0 + (t1 - t0) * (static_cast<double>(k) / (points - 1));
    sunrealtype reached = times[k];
    if (CVode(mem, times[k], y, &reached, CV_NORMAL) < 0 || ctx.error) {
      rethrow_budget(ctx);
      bad(ctx.message.empty() ? "CVODE failed"
                              : "CVODE callback: " + ctx.message);
    }
    for (int j = 0; j < q; ++j) {
      out[k][j] = NV_Ith_S(y, j);
      finite(out[k][j], "simulation value");
    }
    out[k] = output_state(n, times[k], out[k]);
  }
  return make_result(*this, times, out);
}

Result Model::steady_state() const {
  Network n = compile(*this);
  for (const auto &r : reactions)
    if ((mentions(r.rate, "t") && !r.local_params.count("t")) ||
        mentions(r.reverse, "t"))
      bad("steady state does not support explicit time dependence");
  for (const auto &r : rules)
    if (mentions(r.expression, "t"))
      bad("steady state does not support explicit time dependence");
  std::vector<double> initial;
  for (const auto &x : species_list)
    initial.push_back(x.initial);
  for (const auto &x : parameters)
    initial.push_back(x.second);
  std::vector<int> active;
  for (size_t i = 0; i < species_list.size(); ++i) {
    const auto &sp = species_list[i];
    bool assignment = false, rate = false;
    for (const auto &r : rules)
      if (r.target == sp.id) {
        assignment = !r.rate;
        rate = r.rate;
      }
    if (!sp.constant && !assignment && (!sp.boundary || rate))
      active.push_back(static_cast<int>(i));
  }
  size_t pi = species_list.size();
  for (const auto &p : parameters) {
    for (const auto &r : rules)
      if (r.target == p.first && r.rate)
        active.push_back(static_cast<int>(pi));
    ++pi;
  }
  auto residual_ok = [&](const std::vector<double> &x) {
    for (size_t i = 0; i < species_list.size(); ++i)
      if (!std::isfinite(x[i]) || x[i] < -atol)
        return false;
    auto d = derivatives(*this, n, 0, x);
    for (int i : active)
      if (std::abs(d[i]) >
          atol * 10 + rtol * std::max(1.0, std::abs(initial[i])))
        return false;
    return true;
  };
  auto x = initial;
  bool solved = residual_ok(x);
  if (!solved && !active.empty()) {
    int q = static_cast<int>(active.size());
    Sun sun;
    SolverResources resource(q);
    N_Vector u = resource.y = N_VNew_Serial(q, sun.ctx);
    N_Vector scale = resource.scale = N_VNew_Serial(q, sun.ctx);
    if (!u || !scale)
      bad("KINSOL allocation failed");
    for (int i = 0; i < q; ++i) {
      NV_Ith_S(u, i) = initial[active[i]];
      NV_Ith_S(scale, i) = 1 / std::max(1.0, std::abs(initial[active[i]]));
    }
    void *mem = resource.kin = KINCreate(sun.ctx);
    SUNMatrix A = resource.matrix = SUNDenseMatrix(q, q, sun.ctx);
    SUNLinearSolver ls = resource.linear = SUNLinSol_Dense(u, A, sun.ctx);
    if (!mem || !A || !ls)
      bad("KINSOL allocation failed");
    KinCtx ctx{this, &n, initial, active, {}, {}};
    if (KINInit(mem, kin_rhs, u) != KIN_SUCCESS ||
        KINSetUserData(mem, &ctx) != KIN_SUCCESS ||
        KINSetFuncNormTol(mem, atol) != KIN_SUCCESS ||
        KINSetLinearSolver(mem, ls, A) != KIN_SUCCESS ||
        KINSetNumMaxIters(mem, 200) != KIN_SUCCESS)
      bad("KINSOL setup failed");
    int flag = KINSol(mem, u, KIN_LINESEARCH, scale, scale);
    rethrow_budget(ctx);
    if (flag >= 0 && ctx.message.empty()) {
      for (int i = 0; i < q; ++i)
        x[active[i]] = NV_Ith_S(u, i);
      // A singular reaction system may have a family of roots. Only trust a
      // nonlinear root if it stays on the same conserved stoichiometric leaf;
      // otherwise the time-domain fallback below selects the physical root.
      solved = residual_ok(x);
      for (size_t i = 0; i < parameters.size(); ++i) {
        const size_t index = species_list.size() + i;
        if (std::find(active.begin(), active.end(), static_cast<int>(index)) ==
            active.end())
          x[index] = initial[index];
      }
      // Conservative reaction networks are rank-deficient: integrating from
      // the initial state preserves their invariants and fixes root ambiguity.
      if (solved) {
        std::vector<std::vector<double>> basis;
        auto add_basis = [&](std::vector<double> v) {
          for (const auto &b : basis) {
            double dot = 0;
            for (size_t i = 0; i < v.size(); ++i)
              dot += v[i] * b[i];
            for (size_t i = 0; i < v.size(); ++i)
              v[i] -= dot * b[i];
          }
          double norm = 0;
          for (double y : v)
            norm += y * y;
          norm = std::sqrt(norm);
          if (norm > 1e-10) {
            for (double &y : v)
              y /= norm;
            basis.push_back(std::move(v));
          }
        };
        for (const auto &st : n.stoich) {
          std::vector<double> v(active.size(), 0);
          for (size_t j = 0; j < active.size(); ++j) {
            const int index = active[j];
            if (index >= static_cast<int>(species_list.size()))
              continue;
            const auto &sp = species_list[index];
            v[j] = (st.right.count(sp.id) ? st.right.at(sp.id) : 0) -
                   (st.left.count(sp.id) ? st.left.at(sp.id) : 0);
            if (!sp.substance_only)
              for (const auto &c : compartments)
                if (c.id == sp.compartment)
                  v[j] /= c.size;
          }
          add_basis(std::move(v));
        }
        for (const auto &rule : rules)
          if (rule.rate) {
            std::vector<double> v(active.size(), 0);
            int target = state_index(n, rule.target);
            for (size_t j = 0; j < active.size(); ++j)
              if (active[j] == target)
                v[j] = 1;
            add_basis(std::move(v));
          }
        std::vector<double> displacement(active.size());
        double scale = 1;
        for (size_t j = 0; j < active.size(); ++j) {
          displacement[j] = x[active[j]] - initial[active[j]];
          scale = std::max(scale, std::abs(initial[active[j]]));
        }
        for (const auto &b : basis) {
          double dot = 0;
          for (size_t j = 0; j < active.size(); ++j)
            dot += displacement[j] * b[j];
          for (size_t j = 0; j < active.size(); ++j)
            displacement[j] -= dot * b[j];
        }
        for (double value : displacement)
          if (std::abs(value) > 1e-7 * scale)
            solved = false;
      }
    }
  }
  if (!solved) {
    Model restart = *this;
    double time = 0, interval = 1;
    for (int iteration = 0; iteration < 14; ++iteration) {
      auto r = restart.simulate(time, time + interval, 2);
      for (size_t i = 0; i < x.size(); ++i)
        x[i] = r.data[r.columns.size() + i + 1];
      if (residual_ok(x)) {
        solved = true;
        break;
      }
      for (size_t i = 0; i < species_list.size(); ++i)
        restart.species_list[i].initial = std::max(0.0, x[i]);
      size_t index = species_list.size();
      for (auto &p : restart.parameters)
        p.second = x[index++];
      time += interval;
      interval *= 10;
    }
  }
  if (!solved)
    bad("steady state did not converge");
  x = output_state(n, 0, x);
  return make_result(*this, {0}, {x});
}

Result Model::ssa(double t1, int points, std::uint64_t seed) const {
  check_grid(*this, 0, t1, points, "ssa");
  Network n = compile(*this);
  if (!rules.empty())
    bad("ssa does not support rules");
  for (const auto &c : compartments)
    if (c.size != 1)
      bad("ssa requires unit compartments");
  for (const auto &sp : species_list)
    if (sp.initial < 0 || std::floor(sp.initial) != sp.initial ||
        sp.initial > 9007199254740991.0)
      bad("ssa requires nonnegative exact integer species counts");
  for (const auto &r : reactions)
    if ((mentions(r.rate, "t") || mentions(r.reverse, "t")) &&
        !r.local_params.count("t"))
      bad("ssa does not support time dependent propensities");
  for (const auto &st : n.stoich)
    for (const auto *side : {&st.left, &st.right})
      for (const auto &term : *side)
        if (std::floor(term.second) != term.second ||
            term.second > 9007199254740991.0)
          bad("SSA requires exact integer stoichiometry");
  std::vector<double> x;
  for (const auto &sp : species_list)
    x.push_back(sp.initial);
  for (const auto &p : parameters)
    x.push_back(p.second);
  std::mt19937_64 rng(seed);
  auto uniform = [&]() {
    return (static_cast<double>(rng() >> 12) + .5) / 4503599627370496.0;
  };
  std::vector<double> times(points);
  std::vector<std::vector<double>> out(points);
  struct Channel {
    size_t reaction;
    bool reverse;
    double propensity;
  };
  std::vector<Channel> channels;
  std::uint64_t events = 0;
  double event_time = std::numeric_limits<double>::infinity();
  size_t chosen = 0;
  auto schedule = [&](double now) {
    channels.clear();
    double total = 0;
    std::vector<double> v(n.vars.size());
    v[0] = now;
    std::copy(x.begin(), x.end(), v.begin() + 1);
    for (size_t j = 0; j < reactions.size(); ++j)
      for (bool reverse : {false, true}) {
        const auto &expression = reverse ? n.reverse_rates[j] : n.rates[j];
        if (!expression)
          continue;
        double propensity = expression->eval(v.data());
        if (!std::isfinite(propensity) || propensity < 0)
          bad("invalid SSA propensity");
        const auto &required = reverse ? n.stoich[j].right : n.stoich[j].left;
        for (const auto &term : required)
          if (x[state_index(n, term.first)] < term.second)
            propensity = 0;
        total += propensity;
        channels.push_back({j, reverse, propensity});
      }
    if (!std::isfinite(total))
      bad("SSA total propensity overflow");
    if (total == 0) {
      event_time = std::numeric_limits<double>::infinity();
      return;
    }
    event_time = now - std::log(uniform()) / total;
    if (event_time <= now)
      bad("SSA clock made no progress");
    double pick = uniform() * total;
    chosen = channels.size() - 1;
    for (size_t j = 0; j < channels.size(); ++j) {
      if (pick < channels[j].propensity) {
        chosen = j;
        break;
      }
      pick -= channels[j].propensity;
    }
  };
  schedule(0);
  for (int k = 0; k < points; ++k) {
    times[k] = t1 * (static_cast<double>(k) / (points - 1));
    while (event_time <= times[k]) {
      if (++events > 10000000)
        bad("SSA event work limit exceeded");
      const double firing = event_time;
      const auto channel = channels[chosen];
      const auto &sto = n.stoich[channel.reaction];
      const auto &remove = channel.reverse ? sto.right : sto.left;
      const auto &add = channel.reverse ? sto.left : sto.right;
      for (const auto &term : remove) {
        int i = state_index(n, term.first);
        if (!species_list[i].boundary && !species_list[i].constant)
          x[i] -= term.second;
      }
      for (const auto &term : add) {
        int i = state_index(n, term.first);
        if (!species_list[i].boundary && !species_list[i].constant)
          x[i] += term.second;
      }
      for (size_t i = 0; i < species_list.size(); ++i)
        if (!std::isfinite(x[i]) || x[i] < 0 || x[i] > 9007199254740991.0)
          bad("SSA count outside exact integer range");
      schedule(firing);
    }
    out[k] = x;
  }
  return make_result(*this, times, out);
}

Result Model::sensitivity(const std::string &p, double t0, double t1,
                          int points, double h) const {
  if (!parameters.count(p))
    bad("unknown sensitivity parameter: " + p);
  if (h <= 0 || !std::isfinite(h))
    bad("sensitivity step must be positive");
  Model a = *this, b = *this;
  double v = parameters.at(p), step = h * std::max(1.0, std::fabs(v));
  if (!std::isfinite(step) || step == 0 || v + step == v || v - step == v)
    bad("sensitivity step is not representable");
  a.parameters[p] = v - step;
  b.parameters[p] = v + step;
  Result ra = a.simulate(t0, t1, points), rb = b.simulate(t0, t1, points),
         r = simulate(t0, t1, points);
  for (int i = 0; i < r.rows; ++i)
    for (size_t c = 1; c < r.columns.size(); ++c)
      r.data[size_t(i) * r.columns.size() + c] =
          (rb.data[size_t(i) * r.columns.size() + c] -
           ra.data[size_t(i) * r.columns.size() + c]) /
          (2 * step);
  return r;
}

} // namespace lmbio
