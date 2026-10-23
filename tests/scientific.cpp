#include "model.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

using lmbio::Model;
using lmbio::Result;

namespace {
int checks = 0;
void near(double actual, double expected, double tolerance) {
  ++checks;
  if (!std::isfinite(actual) || !std::isfinite(expected) ||
      std::abs(actual - expected) > tolerance) {
    std::cerr << "scientific check failed: " << actual << " != " << expected
              << " +/- " << tolerance << '\n';
    std::abort();
  }
}

void first_order() {
  Model m;
  m.name = "first_order";
  m.compartment("cell", 1.0)
      .species("A", "cell", 1.0)
      .species("B", "cell", 0.0)
      .param("k", 0.7)
      .reaction("r", "A -> B", "k*A");
  Result r = m.simulate(0.0, 4.0, 101);
  near(r.final("A"), std::exp(-2.8), 2e-4);
  near(r.final("A") + r.final("B"), 1.0, 2e-5);
}

void reversible_equilibrium() {
  Model m;
  m.name = "reversible";
  m.compartment("cell", 1.0)
      .species("A", "cell", 1.0)
      .species("B", "cell", 0.0)
      .param("kf", 2.0)
      .param("kr", 1.0)
      .reaction("forward", "A -> B", "kf*A")
      .reversible("forward", "kr*B");
  Result r = m.steady_state();
  near(r.final("A"), 1.0 / 3.0, 2e-3);
  near(r.final("B"), 2.0 / 3.0, 2e-3);
}

void michaelis_menten_conservation() {
  Model m;
  m.name = "mass_action";
  m.compartment("cell", 1.0)
      .species("S", "cell", 10.0)
      .species("E", "cell", 1.0)
      .species("ES", "cell", 0.0)
      .species("P", "cell", 0.0)
      .param("kf", 2.0)
      .param("kr", 1.0)
      .param("kc", 0.5)
      .reaction("bind", "S + E -> ES", "kf*S*E")
      .reversible("bind", "kr*ES")
      .reaction("cat", "ES -> E + P", "kc*ES");
  Result r = m.simulate(0.0, 20.0, 201);
  near(r.final("S") + r.final("ES") + r.final("P"), 10.0, 2e-3);
  near(r.final("E") + r.final("ES"), 1.0, 2e-3);
}

void sensitivity_sign() {
  Model m;
  m.name = "sensitivity";
  m.compartment("cell", 1.0)
      .species("A", "cell", 1.0)
      .param("k", 0.5)
      .reaction("r", "A ->", "k*A");
  Result s = m.sensitivity("k", 0.0, 2.0, 21, 0.0001);
  near(s.final("A"), -2.0 * std::exp(-1.0), 2e-2);
}

void sensitivity_at_zero() {
  Model m;
  m.name = "sensitivity_zero";
  m.compartment("cell", 1.0)
      .species("A", "cell", 1.0)
      .param("k", 0.0)
      .reaction("r", "A ->", "k*A");
  Result s = m.sensitivity("k", 0.0, 2.0, 21, 0.00001);
  near(s.final("A"), -2.0, 2e-2);
}

void robertson_stiff() {
  Model m;
  m.name = "robertson";
  m.compartment("v", 1.0)
      .species("A", "v", 1.0)
      .species("B", "v", 0.0)
      .species("C", "v", 0.0)
      .reaction("r1", "A -> B", "0.04*A")
      .reaction("r2", "B + B -> B + C", "30000000.0*B*B")
      .reaction("r3", "B + C -> A + C", "10000.0*B*C");
  Result r = m.simulate(0.0, 40.0, 41);
  near(r.final("A"), 0.715827, 0.01);
  near(r.final("A") + r.final("B") + r.final("C"), 1.0, 2e-3);
}

void ssa_poisson_moments() {
  Model m;
  m.name = "immigration_death";
  m.compartment("cell", 1.0)
      .species("X", "cell", 0.0)
      .param("birth", 3.0)
      .param("death", 1.0)
      .reaction("immigration", " -> X", "birth")
      .reaction("loss", "X ->", "death*X");
  std::vector<double> samples;
  samples.reserve(2000);
  for (std::uint64_t seed = 1; seed <= 2000; ++seed)
    samples.push_back(m.ssa(10.0, 2, seed).final("X"));
  double mean = 0.0;
  for (double x : samples)
    mean += x;
  mean /= samples.size();
  double variance = 0.0;
  for (double x : samples)
    variance += (x - mean) * (x - mean);
  variance /= samples.size() - 1;
  const double lambda = 3.0 * (1.0 - std::exp(-10.0));
  near(mean, lambda, 3.0 * std::sqrt(lambda / samples.size()));
  near(variance, lambda,
       3.0 *
           std::sqrt((lambda + 2.0 * lambda * lambda) / (samples.size() - 1)));
}
} // namespace

int main() {
  first_order();
  reversible_equilibrium();
  michaelis_menten_conservation();
  sensitivity_sign();
  sensitivity_at_zero();
  robertson_stiff();
  ssa_poisson_moments();
  std::cout << checks << " scientific checks passed\n";
  return 0;
}
