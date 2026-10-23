#include "expr.hpp"
#include "model.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lmbio {
namespace {

using pugi::xml_node;

std::string local(const xml_node &n) {
  std::string s = n.name();
  const auto p = s.find(':');
  return p == std::string::npos ? s : s.substr(p + 1);
}
xml_node child_local(const xml_node &p, const char *want) {
  for (auto c : p.children())
    if (c.type() == pugi::node_element && local(c) == want)
      return c;
  return {};
}
[[noreturn]] void bad(const std::string &what) {
  throw std::runtime_error("SBML: " + what);
}
std::string namespace_uri(xml_node node) {
  const std::string name = node.name();
  const auto colon = name.find(':');
  const std::string key =
      colon == std::string::npos ? "xmlns" : "xmlns:" + name.substr(0, colon);
  for (auto parent = node; parent; parent = parent.parent())
    if (parent.attribute(key.c_str()))
      return parent.attribute(key.c_str()).value();
  return {};
}
void check_children(xml_node node,
                    std::initializer_list<const char *> allowed) {
  for (auto child : node.children())
    if (child.type() == pugi::node_element) {
      const auto name = local(child);
      if (name == "notes" || name == "annotation")
        continue;
      bool found = false;
      for (const char *x : allowed)
        if (name == x)
          found = true;
      if (!found)
        bad("unsupported " + local(node) + " element " + name);
    }
}
void check_namespaces(xml_node node, const std::string &expected) {
  if (node.type() != pugi::node_element)
    return;
  if (local(node) == "notes" || local(node) == "annotation")
    return;
  const std::string current =
      local(node) == "math" ? "http://www.w3.org/1998/Math/MathML" : expected;
  if (namespace_uri(node) != current)
    bad("unsupported namespace on " + local(node));
  for (auto child : node.children())
    check_namespaces(child, current);
}

double number(const char *s, const std::string &what) {
  if (!s || !*s)
    bad("missing " + what);
  char *e = nullptr;
  errno = 0;
  const double x = std::strtod(s, &e);
  if (errno || !e || *e)
    bad("invalid " + what + " '" + s + "'");
  if (!std::isfinite(x))
    bad("non-finite " + what);
  return x;
}
bool yes(const char *s, bool d = false) {
  if (!s || !*s)
    return d;
  const std::string v(s);
  if (v == "true" || v == "1")
    return true;
  if (v == "false" || v == "0")
    return false;
  bad("invalid boolean '" + v + "'");
}
std::string attr_required(const xml_node &n, const char *k) {
  const auto a = n.attribute(k);
  if (!a)
    bad("" + local(n) + " missing " + k);
  return a.value();
}
std::string fmt(double x) {
  std::ostringstream o;
  o << std::setprecision(17) << x;
  return o.str();
}
void reject_attrs(const xml_node &n,
                  std::initializer_list<const char *> allowed) {
  for (auto a : n.attributes()) {
    const std::string k = a.name();
    if (k == "xmlns" || k.rfind("xmlns:", 0) == 0)
      continue;
    bool ok = false;
    for (const char *x : allowed)
      if (k == x)
        ok = true;
    if (!ok)
      bad(local(n) + " attribute '" + k + "' is unsupported");
  }
}

std::string math(const xml_node &n);
std::string math_children(const xml_node &n) {
  std::vector<std::string> a;
  for (auto c : n.children())
    if (c.type() == pugi::node_element)
      a.push_back(math(c));
  if (a.size() != 1)
    bad("MathML math needs exactly one expression");
  return a[0];
}
std::string math(const xml_node &n) {
  const std::string k = local(n);
  if (k == "math")
    return math_children(n);
  if (k == "cn" || k == "ci") {
    std::string text = n.text().as_string();
    auto trim = [](std::string x) {
      auto start = x.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
        return std::string();
      return x.substr(start, x.find_last_not_of(" \t\r\n") - start + 1);
    };
    text = trim(text);
    if (text.empty())
      bad("empty MathML " + k);
    if (k == "ci")
      return text;
    auto sep = child_local(n, "sep");
    const std::string type = n.attribute("type").as_string("real");
    double value = number(text.c_str(), "MathML number");
    if (sep) {
      std::string tail;
      for (auto child = sep.next_sibling(); child; child = child.next_sibling())
        if (child.type() == pugi::node_pcdata)
          tail += child.value();
      double right =
          number(trim(tail).c_str(), "MathML exponent or denominator");
      if (type == "e-notation")
        value *= std::pow(10.0, right);
      else if (type == "rational") {
        if (right == 0)
          bad("zero MathML denominator");
        value /= right;
      } else
        bad("unsupported MathML cn type " + type);
    } else if (type != "real" && type != "integer")
      bad("MathML cn requires sep for " + type);
    if (!std::isfinite(value))
      bad("nonfinite MathML number");
    return "(" + fmt(value) + ")";
  }
  if (k == "csymbol") {
    const std::string def = n.attribute("definitionURL").value();
    const std::string t = n.text().as_string();
    if (def == "http://www.sbml.org/sbml/symbols/time")
      return "t";
    bad("unsupported MathML csymbol '" + t + "'");
  }
  if (k == "true" || k == "false")
    return k == "true" ? "1" : "0";
  if (k == "pi")
    return "3.14159265358979323846";
  if (k == "exponentiale")
    return "2.71828182845904523536";
  if (k == "piecewise" || k == "piece" || k == "otherwise")
    bad("unsupported MathML operator " + k);
  if (k == "apply") {
    std::vector<xml_node> c;
    for (auto x : n.children())
      if (x.type() == pugi::node_element)
        c.push_back(x);
    if (c.size() < 2)
      bad("MathML apply has no operands");
    const std::string op = local(c[0]);
    if (op == "root" && c.size() == 3 && local(c[1]) == "degree") {
      auto degree = child_local(c[1], "cn");
      if (!degree)
        degree = child_local(c[1], "ci");
      if (!degree)
        bad("MathML root degree is empty");
      std::string d = math(degree);
      std::string rad = math(c[2]);
      return "pow(" + rad + ",1/(" + d + "))";
    }
    std::vector<std::string> a;
    for (size_t i = 1; i < c.size(); ++i) {
      if (op == "log" && local(c[i]) == "logbase") {
        if (c[i].children().begin() == c[i].children().end())
          bad("empty MathML logbase");
        continue;
      }
      a.push_back(math(c[i]));
    }
    if (op == "log" && c.size() >= 3 && local(c[1]) == "logbase") {
      if (a.size() != 1)
        bad("MathML log expects one operand");
      const std::string base = math(*c[1].children().begin());
      return "(log(" + a[0] + ")/log(" + base + "))";
    }
    auto one = [&]() {
      if (a.size() != 1)
        bad("MathML " + op + " expects one operand");
      return a[0];
    };
    auto two = [&]() {
      if (a.size() != 2)
        bad("MathML " + op + " expects two operands");
      return std::string("(") + a[0] + "," + a[1] + ")";
    };
    if (op == "plus" || op == "times") {
      std::string r = "(" + a[0] + ")";
      for (size_t i = 1; i < a.size(); ++i)
        r += op == "plus" ? "+(" + a[i] + ")" : "*(" + a[i] + ")";
      return r;
    }
    if (op == "minus") {
      if (a.size() == 1)
        return "-(" + a[0] + ")";
      if (a.size() == 2)
        return "(" + a[0] + ")-(" + a[1] + ")";
      bad("MathML minus expects one or two operands");
    }
    if (op == "divide") {
      two();
      return "(" + a[0] + ")/(" + a[1] + ")";
    }
    if (op == "power") {
      two();
      return "pow(" + a[0] + "," + a[1] + ")";
    }
    if (op == "root") {
      if (a.size() == 1)
        return "sqrt(" + a[0] + ")";
      if (a.size() == 2 && local(c[1]) == "degree")
        return "pow(" + a[1] + ",1/" + a[0] + ")";
      bad("MathML root expects one operand or degree");
    }
    const std::string unary = op == "arcsin"   ? "asin"
                              : op == "arccos" ? "acos"
                              : op == "arctan" ? "atan"
                                               : op;
    static const char *fns[] = {"exp",  "ln",   "log",  "sin",   "cos",
                                "tan",  "asin", "acos", "atan",  "sinh",
                                "cosh", "tanh", "abs",  "floor", "ceiling"};
    bool known = false;
    for (auto f : fns)
      if (unary == f || (unary == "ceiling" && std::string(f) == "ceiling"))
        known = true;
    if (known) {
      one();
      return (unary == "ceiling" ? "ceil"
              : unary == "log"   ? "log10"
                                 : unary) +
             std::string("(") + a[0] + ")";
    }
    bad("unsupported MathML operator " + op);
  }
  bad("unsupported MathML element " + k);
}

struct Tok {
  enum K { end, id, num, op, lp, rp, comma } k;
  std::string s;
};
class Lex {
public:
  explicit Lex(const std::string &s) : s_(s) { next(); }
  Tok cur() const { return t_; }
  void next() {
    while (p_ < s_.size() && std::isspace((unsigned char)s_[p_]))
      ++p_;
    if (p_ == s_.size()) {
      t_ = {Tok::end, {}};
      return;
    }
    char c = s_[p_];
    if (std::isdigit((unsigned char)c) || c == '.') {
      size_t q = p_;
      char *e = nullptr;
      std::strtod(s_.c_str() + p_, &e);
      if (e == s_.c_str() + p_)
        bad("invalid numeric literal");
      p_ = size_t(e - s_.c_str());
      t_ = {Tok::num, s_.substr(q, p_ - q)};
      return;
    }
    if (std::isalpha((unsigned char)c) || c == '_') {
      size_t q = p_++;
      while (p_ < s_.size() &&
             (std::isalnum((unsigned char)s_[p_]) || s_[p_] == '_'))
        ++p_;
      t_ = {Tok::id, s_.substr(q, p_ - q)};
      return;
    }
    ++p_;
    if (c == '(')
      t_ = {Tok::lp, "("};
    else if (c == ')')
      t_ = {Tok::rp, ")"};
    else if (c == ',')
      t_ = {Tok::comma, ","};
    else if (c == '^')
      t_ = {Tok::op, "^"};
    else
      t_ = {Tok::op, std::string(1, c)};
  }

private:
  std::string s_;
  size_t p_ = 0;
  Tok t_{};
};
struct A {
  std::string op;
  std::vector<std::unique_ptr<A>> a;
  std::string v;
};
class Infix {
public:
  explicit Infix(const std::string &s) : l_(s) {}
  std::unique_ptr<A> parse() {
    auto x = add();
    if (l_.cur().k != Tok::end)
      bad("invalid expression near '" + l_.cur().s + "'");
    return x;
  }

private:
  Lex l_;
  std::unique_ptr<A> atom() {
    Tok t = l_.cur();
    if (t.k == Tok::num || t.k == Tok::id) {
      l_.next();
      if (l_.cur().k == Tok::lp) {
        auto x = std::make_unique<A>();
        x->op = t.s;
        l_.next();
        if (l_.cur().k != Tok::rp) {
          for (;;) {
            x->a.push_back(add());
            if (l_.cur().k != Tok::comma)
              break;
            l_.next();
          }
          if (l_.cur().k != Tok::rp)
            bad("missing ')' in expression");
        }
        l_.next();
        return x;
      }
      auto x = std::make_unique<A>();
      x->op = t.k == Tok::num ? "#" : "$";
      x->v = t.s;
      return x;
    }
    if (t.k == Tok::lp) {
      l_.next();
      auto x = add();
      if (l_.cur().k != Tok::rp)
        bad("missing ')' in expression");
      l_.next();
      return x;
    }
    bad("invalid expression atom");
  }
  std::unique_ptr<A> unary() {
    if (l_.cur().k == Tok::op && (l_.cur().s == "+" || l_.cur().s == "-")) {
      std::string o = l_.cur().s;
      l_.next();
      auto x = std::make_unique<A>();
      x->op = o == "-" ? "neg" : "pos";
      x->a.push_back(unary());
      return x;
    }
    return power();
  }
  std::unique_ptr<A> power() {
    auto x = atom();
    if (l_.cur().k == Tok::op && l_.cur().s == "^") {
      l_.next();
      auto y = unary();
      auto z = std::make_unique<A>();
      z->op = "power";
      z->a.push_back(std::move(x));
      z->a.push_back(std::move(y));
      return z;
    }
    return x;
  }
  std::unique_ptr<A> mul() {
    auto x = unary();
    while (l_.cur().k == Tok::op && (l_.cur().s == "*" || l_.cur().s == "/")) {
      std::string o = l_.cur().s;
      l_.next();
      auto z = std::make_unique<A>();
      z->op = o;
      z->a.push_back(std::move(x));
      z->a.push_back(unary());
      x = std::move(z);
    }
    return x;
  }
  std::unique_ptr<A> add() {
    auto x = mul();
    while (l_.cur().k == Tok::op && (l_.cur().s == "+" || l_.cur().s == "-")) {
      std::string o = l_.cur().s;
      l_.next();
      auto z = std::make_unique<A>();
      z->op = o;
      z->a.push_back(std::move(x));
      z->a.push_back(mul());
      x = std::move(z);
    }
    return x;
  }
};
void emit_math(pugi::xml_node p, const std::string &s,
               const std::set<std::string> &bound) {
  auto m = p.append_child("math");
  m.append_attribute("xmlns") = "http://www.w3.org/1998/Math/MathML";
  std::function<void(pugi::xml_node, const A &)> put = [&](pugi::xml_node q,
                                                           const A &x) {
    if (x.op == "#") {
      auto cn = q.append_child("cn");
      auto e = x.v.find_first_of("eE");
      if (e == std::string::npos)
        cn.text().set(x.v.c_str());
      else {
        cn.append_attribute("type") = "e-notation";
        cn.append_child(pugi::node_pcdata).set_value(x.v.substr(0, e).c_str());
        cn.append_child("sep");
        cn.append_child(pugi::node_pcdata).set_value(x.v.substr(e + 1).c_str());
      }
      return;
    }
    if (x.op == "$") {
      if (x.v == "t") {
        auto z = q.append_child("csymbol");
        z.append_attribute("definitionURL") =
            "http://www.sbml.org/sbml/symbols/time";
        z.text() = "time";
      } else if (x.v == "pi" && !bound.count(x.v))
        q.append_child("pi");
      else if (x.v == "e" && !bound.count(x.v))
        q.append_child("exponentiale");
      else
        q.append_child("ci").text() = x.v.c_str();
      return;
    }
    if (x.op == "neg") {
      auto a = q.append_child("apply");
      a.append_child("minus");
      put(a, *x.a[0]);
      return;
    }
    if (x.op == "pos") {
      put(q, *x.a[0]);
      return;
    }
    auto a = q.append_child("apply");
    std::string op = x.op;
    if (op == "+")
      op = "plus";
    else if (op == "-")
      op = "minus";
    else if (op == "*")
      op = "times";
    else if (op == "/")
      op = "divide";
    else if (op == "asin")
      op = "arcsin";
    else if (op == "acos")
      op = "arccos";
    else if (op == "atan")
      op = "arctan";
    else if (op == "pow")
      op = "power";
    else if (op == "log")
      op = "ln";
    else if (op == "log10")
      op = "log";
    else if (op == "ceil")
      op = "ceiling";
    else if (op == "sqrt")
      op = "root";
    static const char *allowed[] = {
        "plus", "minus", "times", "divide", "power",  "exp",     "ln",
        "log",  "sin",   "cos",   "tan",    "arcsin", "arccos",  "arctan",
        "sinh", "cosh",  "tanh",  "abs",    "floor",  "ceiling", "root"};
    bool known = false;
    for (auto f : allowed)
      if (op == f)
        known = true;
    if (!known)
      bad("cannot serialize unsupported expression function '" + x.op + "'");
    a.append_child(op.c_str());
    for (auto &y : x.a)
      put(a, *y);
  };
  Infix in(s);
  put(m, *in.parse());
}
std::string equation(const xml_node &r, bool reverse = false) {
  (void)reverse;
  std::string out;
  for (auto sr : r.children()) {
    if (local(sr) != "speciesReference")
      continue;
    reject_attrs(sr, {"species", "stoichiometry", "constant"});
    if (sr.attribute("constant") &&
        !yes(sr.attribute("constant").value(), true))
      bad("dynamic speciesReference stoichiometry is unsupported");
    if (sr.attribute("id"))
      bad("speciesReference id is unsupported");
    if (child_local(sr, "stoichiometryMath"))
      bad("stoichiometryMath is unsupported");
    if (!out.empty())
      out += " + ";
    double st =
        sr.attribute("stoichiometry")
            ? number(sr.attribute("stoichiometry").value(), "stoichiometry")
            : 1;
    if (!std::isfinite(st) || st < 0)
      bad("stoichiometry must be finite and nonnegative");
    if (st != 1) {
      std::ostringstream q;
      q << std::setprecision(17) << st;
      out += q.str() + " ";
    }
    out += attr_required(sr, "species");
  }
  return out;
}

} // namespace

Model load_sbml(const std::string &path) {
  pugi::xml_document d;
  auto res =
      d.load_file(path.c_str(), pugi::parse_default | pugi::parse_declaration);
  if (!res)
    bad("cannot read '" + path + "': " + res.description());
  xml_node root;
  for (auto c : d.children())
    if (c.type() == pugi::node_element && local(c) == "sbml") {
      root = c;
      break;
    }
  if (!root)
    bad("missing sbml root");
  int level = root.attribute("level").as_int(-1),
      ver = root.attribute("version").as_int(-1);
  if (level != 3 || (ver != 1 && ver != 2))
    bad("only SBML Level 3 Version 1 or 2 is supported");
  std::string ns = root.attribute("xmlns").value();
  const std::string root_name = root.name();
  const auto colon = root_name.find(':');
  if (ns.empty() && colon != std::string::npos)
    ns =
        root.attribute(("xmlns:" + root_name.substr(0, colon)).c_str()).value();
  if (!ns.empty() &&
      ns != (ver == 1 ? "http://www.sbml.org/sbml/level3/version1/core"
                      : "http://www.sbml.org/sbml/level3/version2/core"))
    bad("unsupported SBML namespace");
  const std::string expected =
      ver == 1 ? "http://www.sbml.org/sbml/level3/version1/core"
               : "http://www.sbml.org/sbml/level3/version2/core";
  check_namespaces(root, expected);
  check_children(root, {"model"});
  if (root.attribute("requiredPackages"))
    bad("requiredPackages is unsupported");
  const std::string core_prefix =
      colon == std::string::npos ? "" : root_name.substr(0, colon);
  for (auto a : root.attributes())
    if (std::string(a.name()).rfind("xmlns:", 0) == 0 &&
        std::string(a.name()) != "xmlns:xml" &&
        std::string(a.name()) !=
            (core_prefix.empty() ? "" : "xmlns:" + core_prefix) &&
        std::string(a.value()) != expected &&
        std::string(a.value()) != "http://www.w3.org/1998/Math/MathML")
      bad("SBML package namespace '" + std::string(a.name()) +
          "' is unsupported");
  auto md = child_local(root, "model");
  if (!md)
    bad("missing model");
  reject_attrs(md, {"id", "name", "timeUnits", "substanceUnits", "extentUnits",
                    "volumeUnits", "areaUnits", "lengthUnits"});
  check_children(md,
                 {"listOfCompartments", "listOfSpecies", "listOfParameters",
                  "listOfReactions", "listOfRules", "listOfUnitDefinitions"});
  Model m;
  m.name = md.attribute("id").value();
  if (m.name.empty())
    m.name = md.attribute("name").value();
  auto loc = child_local(md, "listOfCompartments");
  for (auto c : loc.children()) {
    if (local(c) != "compartment") {
      bad("unsupported listOfCompartments element " + local(c));
      continue;
    }
    reject_attrs(
        c, {"id", "name", "size", "constant", "spatialDimensions", "units"});
    if (c.attribute("constant") && !yes(c.attribute("constant").value(), true))
      bad("dynamic compartments are unsupported");
    m.compartments.push_back(
        {attr_required(c, "id"),
         c.attribute("size")
             ? number(c.attribute("size").value(), "compartment size")
             : 1});
  }
  auto lp = child_local(md, "listOfParameters");
  for (auto p : lp.children()) {
    if (local(p) != "parameter") {
      bad("unsupported listOfParameters element " + local(p));
      continue;
    }
    reject_attrs(p, {"id", "name", "value", "constant", "units"});
    const std::string id = attr_required(p, "id");
    if (m.parameters.count(id))
      bad("duplicate parameter id: " + id);
    m.parameters[id] = number(p.attribute("value").value(), "parameter value");
  }
  auto ls = child_local(md, "listOfSpecies");
  for (auto s : ls.children()) {
    if (local(s) != "species")
      bad("unsupported listOfSpecies element " + local(s));
    reject_attrs(s, {"id", "name", "compartment", "initialAmount",
                     "initialConcentration", "substanceUnits",
                     "hasOnlySubstanceUnits", "boundaryCondition", "constant"});
    if (s.attribute("conversionFactor"))
      bad("species conversionFactor changes semantics and is unsupported");
    Species x;
    x.id = attr_required(s, "id");
    x.compartment = attr_required(s, "compartment");
    x.substance_only = yes(s.attribute("hasOnlySubstanceUnits").value());
    x.boundary = yes(s.attribute("boundaryCondition").value());
    x.constant = yes(s.attribute("constant").value());
    auto ia = s.attribute("initialAmount"),
         ic = s.attribute("initialConcentration");
    if (ia && ic)
      bad("species has both initialAmount and initialConcentration");
    double v = ia   ? number(ia.value(), "initialAmount")
               : ic ? number(ic.value(), "initialConcentration")
                    : 0;
    x.initial = v;
    if (ia && !x.substance_only) {
      double vol = 1;
      for (auto &c : m.compartments)
        if (c.id == x.compartment)
          vol = c.size;
      x.initial = v / vol;
    }
    if (ic && x.substance_only) {
      double vol = 1;
      for (auto &c : m.compartments)
        if (c.id == x.compartment)
          vol = c.size;
      x.initial = v * vol;
    }
    m.species_list.push_back(x);
  }
  auto lr = child_local(md, "listOfReactions");
  for (auto r : lr.children()) {
    if (local(r) != "reaction") {
      bad("unsupported listOfReactions element " + local(r));
      continue;
    }
    Reaction x;
    x.id = attr_required(r, "id");
    reject_attrs(r, {"id", "name", "reversible", "fast"});
    if (yes(r.attribute("fast").value()))
      bad("fast reactions are unsupported");
    auto kl = child_local(r, "kineticLaw");
    if (!kl)
      bad("reaction '" + x.id + "' missing kineticLaw");
    check_children(r, {"listOfReactants", "listOfProducts", "listOfModifiers",
                       "kineticLaw"});
    check_children(kl, {"math", "listOfLocalParameters", "listOfParameters"});
    reject_attrs(kl, {});
    auto mm = child_local(kl, "math");
    if (!mm)
      bad("reaction '" + x.id + "' missing kineticLaw MathML");
    x.rate = math(mm);
    for (auto p : child_local(kl, "listOfLocalParameters").children()) {
      if (local(p) != "localParameter")
        bad("unsupported kineticLaw parameter element " + local(p));
      reject_attrs(p, {"id", "name", "value", "units"});
      const std::string id = attr_required(p, "id");
      if (x.local_params.count(id))
        bad("duplicate local parameter id: " + id);
      x.local_params[id] =
          number(p.attribute("value").value(), "local parameter value");
    }
    for (auto p : child_local(kl, "listOfParameters").children()) {
      if (local(p) != "parameter")
        bad("unsupported kineticLaw parameter element " + local(p));
      reject_attrs(p, {"id", "name", "value", "units"});
      const std::string id = attr_required(p, "id");
      if (x.local_params.count(id))
        bad("duplicate local parameter id: " + id);
      x.local_params[id] =
          number(p.attribute("value").value(), "local parameter value");
    }
    x.equation = equation(child_local(r, "listOfReactants")) + " -> " +
                 equation(child_local(r, "listOfProducts"));
    m.reactions.push_back(std::move(x));
  }
  auto rules = child_local(md, "listOfRules");
  for (auto r : rules.children()) {
    const auto k = local(r);
    if (k == "algebraicRule")
      bad("algebraic rules are unsupported");
    if (k != "assignmentRule" && k != "rateRule") {
      bad("unsupported listOfRules element " + k);
      continue;
    }
    auto mm = child_local(r, "math");
    if (!mm)
      bad(k + " missing MathML");
    Rule x{attr_required(r, "variable"), math(mm), k == "rateRule"};
    m.rules.push_back(std::move(x));
  }
  if (child_local(md, "listOfEvents"))
    bad("events are unsupported");
  if (child_local(md, "listOfInitialAssignments"))
    bad("initial assignments are unsupported");
  if (child_local(md, "listOfConstraints"))
    bad("constraints are unsupported");
  if (child_local(md, "listOfFunctionDefinitions"))
    bad("function definitions are unsupported");
  for (auto parameter : lp.children())
    if (yes(parameter.attribute("constant").as_string("true"), true))
      for (const auto &rule : m.rules)
        if (rule.target == parameter.attribute("id").as_string())
          bad("rule targets a constant parameter");
  m.validate();
  return m;
}

void save_sbml(const Model &m, const std::string &path) {
  m.validate();
  pugi::xml_document doc;
  auto declaration = doc.append_child(pugi::node_declaration);
  declaration.append_attribute("version") = "1.0";
  declaration.append_attribute("encoding") = "UTF-8";
  auto root = doc.append_child("sbml");
  root.append_attribute("xmlns") =
      "http://www.sbml.org/sbml/level3/version2/core";
  root.append_attribute("level") = "3";
  root.append_attribute("version") = "2";
  auto md = root.append_child("model");
  std::string model_id = m.name;
  if (model_id.empty() ||
      !(std::isalpha(static_cast<unsigned char>(model_id[0])) ||
        model_id[0] == '_'))
    model_id = "model";
  for (char &c : model_id)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
      c = '_';
  md.append_attribute("id") = model_id.c_str();
  if (!m.name.empty())
    md.append_attribute("name") = m.name.c_str();
  auto compartments = md.append_child("listOfCompartments");
  for (const auto &c : m.compartments) {
    auto n = compartments.append_child("compartment");
    n.append_attribute("id") = c.id.c_str();
    n.append_attribute("size") = fmt(c.size).c_str();
    n.append_attribute("constant") = true;
  }
  auto species = md.append_child("listOfSpecies");
  for (const auto &x : m.species_list) {
    auto n = species.append_child("species");
    n.append_attribute("id") = x.id.c_str();
    n.append_attribute("compartment") = x.compartment.c_str();
    n.append_attribute("hasOnlySubstanceUnits") = x.substance_only;
    n.append_attribute("boundaryCondition") = x.boundary;
    n.append_attribute("constant") = x.constant;
    n.append_attribute(x.substance_only ? "initialAmount"
                                        : "initialConcentration") =
        fmt(x.initial).c_str();
  }
  if (!m.parameters.empty()) {
    auto parameters = md.append_child("listOfParameters");
    for (const auto &x : m.parameters) {
      auto n = parameters.append_child("parameter");
      n.append_attribute("id") = x.first.c_str();
      n.append_attribute("value") = fmt(x.second).c_str();
      bool constant = true;
      for (const auto &rule : m.rules)
        if (rule.target == x.first)
          constant = false;
      n.append_attribute("constant") = constant;
    }
  }
  std::set<std::string> bound;
  for (const auto &x : m.species_list)
    bound.insert(x.id);
  for (const auto &x : m.compartments)
    bound.insert(x.id);
  for (const auto &x : m.parameters)
    bound.insert(x.first);
  if (!m.rules.empty()) {
    auto rules = md.append_child("listOfRules");
    for (const auto &x : m.rules) {
      auto n = rules.append_child(x.rate ? "rateRule" : "assignmentRule");
      n.append_attribute("variable") = x.target.c_str();
      emit_math(n, x.expression, bound);
    }
  }
  std::set<std::string> used;
  for (const auto &x : m.species_list)
    used.insert(x.id);
  for (const auto &x : m.compartments)
    used.insert(x.id);
  for (const auto &x : m.parameters)
    used.insert(x.first);
  for (const auto &x : m.reactions)
    used.insert(x.id);
  if (!m.reactions.empty()) {
    auto reactions = md.append_child("listOfReactions");
    auto split = [](const std::string &equation) {
      auto p = equation.find("->");
      if (p == std::string::npos)
        p = equation.find("=>");
      if (p == std::string::npos)
        bad("reaction missing arrow");
      return std::make_pair(equation.substr(0, p), equation.substr(p + 2));
    };
    auto emit_side = [](pugi::xml_node parent, const char *list,
                        const std::string &side) {
      if (side.find_first_not_of(" \t\r\n") == std::string::npos)
        return;
      auto container = parent.append_child(list);
      std::istringstream input(side);
      std::string term;
      while (std::getline(input, term, '+')) {
        for (char &c : term)
          if (c == '*')
            c = ' ';
        std::istringstream tokens(term);
        std::string first, id;
        tokens >> first;
        double coefficient = 1;
        if (tokens >> id)
          coefficient = number(first.c_str(), "stoichiometry");
        else
          id = first;
        if (id.empty())
          bad("empty reaction species");
        auto n = container.append_child("speciesReference");
        n.append_attribute("species") = id.c_str();
        n.append_attribute("stoichiometry") = fmt(coefficient).c_str();
        n.append_attribute("constant") = true;
      }
    };
    auto emit_reaction = [&](const Reaction &r, const std::string &id,
                             const std::string &left, const std::string &right,
                             const std::string &expression) {
      auto n = reactions.append_child("reaction");
      n.append_attribute("id") = id.c_str();
      n.append_attribute("reversible") = false;
      emit_side(n, "listOfReactants", left);
      emit_side(n, "listOfProducts", right);
      auto law = n.append_child("kineticLaw");
      auto local_bound = bound;
      for (const auto &x : r.local_params)
        local_bound.insert(x.first);
      emit_math(law, expression, local_bound);
      if (!r.local_params.empty()) {
        auto locals = law.append_child("listOfLocalParameters");
        for (const auto &x : r.local_params) {
          auto p = locals.append_child("localParameter");
          p.append_attribute("id") = x.first.c_str();
          p.append_attribute("value") = fmt(x.second).c_str();
        }
      }
    };
    for (const auto &r : m.reactions) {
      auto sides = split(r.equation);
      emit_reaction(r, r.id, sides.first, sides.second, r.rate);
      if (!r.reverse.empty()) {
        std::string id = r.id + "__reverse";
        while (used.count(id))
          id += '_';
        used.insert(id);
        emit_reaction(r, id, sides.second, sides.first, r.reverse);
      }
    }
  }
  if (!doc.save_file(path.c_str(), "  ", pugi::format_default,
                     pugi::encoding_utf8))
    bad("cannot write output '" + path + "'");
}
} // namespace lmbio
