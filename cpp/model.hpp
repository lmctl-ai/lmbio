#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace lmbio {
struct Compartment {
  std::string id;
  double size = 1;
};
// initial is the species symbol value: concentration unless substance_only.
struct Species {
  std::string id, compartment;
  double initial = 0;
  bool boundary = false, constant = false, substance_only = false;
};
struct Reaction {
  std::string id, equation, rate, reverse;
  std::map<std::string, double> local_params;
  // Optional stochastic propensities written by Model::mass_action; when empty
  // ssa() uses rate/reverse as given.
  std::string ssa_rate = {}, ssa_reverse = {};
};
struct Rule {
  std::string target, expression;
  bool rate = false;
};
struct Result {
  std::vector<std::string> columns; // time followed by species then parameters
  std::vector<double> data;
  int rows = 0;
  double value(const std::string &id, int row) const;
  double final(const std::string &id) const;
  double maximum(const std::string &id) const;
  double time_of_max(const std::string &id) const;
  double minimum(const std::string &id) const;
  double integral(const std::string &id) const;
  double crossing(const std::string &id, double level) const;
  Result append(const Result &next) const;
  void save_csv(const std::string &path) const;
};
struct Model {
  std::string name;
  std::vector<Compartment> compartments;
  std::vector<Species> species_list;
  std::map<std::string, double> parameters;
  std::vector<Reaction> reactions;
  std::vector<Rule> rules;
  double rtol = 1e-8, atol = 1e-10;
  Model &compartment(const std::string &, double);
  Model &species(const std::string &, const std::string &, double);
  Model &param(const std::string &, double);
  Model &reaction(const std::string &, const std::string &,
                  const std::string &);
  Model &reversible(const std::string &, const std::string &);
  Model &mass_action(const std::string &id, const std::string &equation,
                     const std::string &k);
  Model &mass_action_reversible(const std::string &id,
                                const std::string &equation,
                                const std::string &kf, const std::string &kr);
  Model &set_state(const Result &);
  Model &rule(const std::string &, const std::string &);
  Model &rate_rule(const std::string &, const std::string &);
  Model &set(const std::string &, double);
  double get(const std::string &) const;
  Model &tol(double, double);
  void validate() const;
  Result simulate(double, double, int) const;
  Result steady_state() const;
  Result ssa(double, int, std::uint64_t) const;
  Result sensitivity(const std::string &parameter, double t0, double t1,
                     int points, double relative_step) const;
};
Model load_sbml(const std::string &path);
void save_sbml(const Model &, const std::string &path);
} // namespace lmbio
