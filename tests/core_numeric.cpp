#include "expr.hpp"
#include "model.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace lmbio;
namespace {
int checks = 0;
void near(double a, double b, double tolerance, const char *what) {
  ++checks;
  if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) > tolerance)
    throw std::runtime_error(what);
}
template <class F> void fails(F f, const char *what) {
  ++checks;
  try {
    f();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error(what);
}
Model decay() {
  Model m;
  m.name = "decay";
  m.compartment("cell", 1)
      .species("A", "cell", 1)
      .species("B", "cell", 0)
      .param("k", .5)
      .reaction("r", "A -> B", "k*A");
  return m;
}
} // namespace
int main() {
  try {
    Model m = decay();
    auto result = m.simulate(0, 2, 21);
    for (int i = 0; i < result.rows; ++i) {
      near(result.value("A", i), std::exp(-.5 * result.value("time", i)), 1e-7,
           "closed-form decay");
      near(result.value("A", i) + result.value("B", i), 1, 1e-8,
           "conservation at every sample");
    }
    near(m.get("A"), 1, 0, "simulate mutated model");
    near(m.simulate(2, 2, 2).value("A", 1), 1, 0, "zero duration");
    auto fractional = m;
    fractional.reactions[0].equation = "0.5 A -> B";
    near(fractional.simulate(0, 2, 3).final("A"), std::exp(-.5), 1e-7,
         "fractional deterministic stoichiometry");
    fails([&] { fractional.ssa(1, 2, 1); },
          "fractional stochastic stoichiometry accepted");
    Model function_name;
    function_name.compartment("c", 1)
        .species("X", "c", 1)
        .param("sin", 0)
        .rule("sin", "sin(X)");
    near(function_name.simulate(0, 1, 2).final("sin"), std::sin(1), 1e-10,
         "function name confused with assignment dependency");
    auto local = m;
    local.reactions[0].local_params["k"] = 1;
    near(local.simulate(0, 2, 3).final("A"), std::exp(-2), 1e-7,
         "local parameter shadowing");
    auto volume = m;
    volume.compartments[0].size = 2;
    near(volume.simulate(0, 2, 3).final("A"), std::exp(-.5), 1e-7,
         "extent divided by volume");
    Model ordered;
    ordered.name = "ordered";
    ordered.compartment("c", 1)
        .species("X", "c", 2)
        .param("first", 0)
        .param("second", 0)
        .rule("first", "second+1")
        .rule("second", "X*2");
    auto algebraic = ordered.simulate(0, 1, 3);
    near(algebraic.final("first"), 5, 0, "assignment topological order");
    near(algebraic.value("second", 0), 4, 0, "initial assignments evaluated");
    Model clock;
    clock.compartment("c", 1).species("X", "c", 0).rate_rule("X", "2*t");
    near(clock.simulate(0, 2, 5).final("X"), 4, 1e-6,
         "time-dependent rate rule");
    Model boundary;
    boundary.compartment("c", 1)
        .species("X", "c", 2)
        .species("Y", "c", 0)
        .reaction("r", "X -> Y", "X");
    boundary.species_list[0].boundary = true;
    near(boundary.simulate(0, 1, 3).final("X"), 2, 0,
         "boundary species remained fixed");
    near(boundary.simulate(0, 1, 3).final("Y"), 2, 1e-7,
         "boundary species reaction flux");
    Model equilibrium;
    equilibrium.compartment("c", 1)
        .species("X", "c", 0)
        .param("birth", 4)
        .param("death", 2)
        .reaction("in", " -> X", "birth")
        .reaction("out", "X ->", "death*X");
    auto stationary = equilibrium.steady_state();
    near(stationary.final("X"), 2, 1e-6, "KINSOL open steady state");
    near(stationary.final("birth"), 4, 0, "KINSOL changed a fixed parameter");
    near(stationary.final("death"), 2, 0, "KINSOL changed a fixed parameter");
    auto reversible = decay();
    reversible.param("kr", .25).reversible("r", "kr*B");
    auto conserved = reversible.steady_state();
    near(conserved.final("A") + conserved.final("B"), 1, 1e-7,
         "steady state conservation");
    near(conserved.final("B") / conserved.final("A"), 2, 1e-6,
         "steady state reversible ratio");
    auto sensitivity = m;
    sensitivity.set("k", 0);
    near(sensitivity.sensitivity("k", 0, 2, 3, 1e-4).final("A"), -2, .001,
         "sensitivity at zero parameter");
    Model stochastic;
    stochastic.compartment("c", 1)
        .species("X", "c", 0)
        .param("birth", 3)
        .param("death", 1)
        .reaction("in", "->X", "birth")
        .reaction("out", "X->", "death*X");
    auto sparse = stochastic.ssa(10, 11, 123),
         dense = stochastic.ssa(10, 101, 123);
    for (int i = 0; i < 11; ++i)
      near(sparse.value("X", i), dense.value("X", 10 * i), 0,
           "SSA sampling changed the seeded path");
    auto repeated = stochastic.ssa(10, 101, 123);
    for (int i = 0; i < 101; ++i)
      near(dense.value("X", i), repeated.value("X", i), 0,
           "SSA repeat determinism");
    Model reverse_counts;
    reverse_counts.compartment("c", 1)
        .species("A", "c", 0)
        .species("B", "c", 100)
        .reaction("r", "A->B", "0")
        .reversible("r", "B");
    near(reverse_counts.ssa(10, 2, 123).final("A"), 100, 0,
         "SSA reverse channel sign");
    Model no_events;
    no_events.compartment("c", 1)
        .species("A", "c", 0)
        .reaction("r", "A->", "1");
    near(no_events.ssa(1, 2, 7).final("A"), 0, 0, "SSA unavailable reactants");
    auto fixed = stochastic;
    fixed.species_list[0].constant = true;
    fixed.set("X", 3);
    near(fixed.ssa(1, 2, 123).final("X"), 3, 0, "SSA constant species");
    fails(
        [&] {
          auto x = m;
          x.param("cell", 1);
          x.validate();
        },
        "duplicate global id accepted");
    fails(
        [&] {
          auto x = m;
          x.rules.push_back({"unknown", "A", false});
          x.validate();
        },
        "unknown rule target accepted");
    fails(
        [&] {
          auto x = m;
          x.reactions[0].equation = "A + -> B";
          x.validate();
        },
        "empty stoichiometric term accepted");
    fails(
        [&] {
          auto x = m;
          x.reactions[0].rate = "sqrt(-1)";
          x.simulate(0, 1, 2);
        },
        "nonfinite rate accepted");
    fails(
        [&] {
          auto x = m;
          x.species_list[0].compartment = "missing";
          x.validate();
        },
        "unknown compartment accepted");
    fails([&] { m.simulate(0, std::numeric_limits<double>::quiet_NaN(), 2); },
          "NaN time accepted");
    fails([&] { m.simulate(0, 1, 1000001); }, "unbounded grid accepted");
    fails(
        [&] {
          auto x = stochastic;
          x.reactions[0].rate = "t+1";
          x.ssa(1, 2, 1);
        },
        "time dependent SSA accepted");
    fails(
        [&] {
          Model x;
          x.compartment("c", 1).species("X", "c", 0).reaction("r", "-> X", "1");
          x.steady_state();
        },
        "nonconvergent steady state accepted");
    std::cout << checks << " core biochemical checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
