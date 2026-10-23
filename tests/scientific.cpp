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
  near(r.final("A"), std::exp(-2.8), 1e-6);
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
  near(r.final("A"), 1.0 / 3.0, 1e-6);
  near(r.final("B"), 2.0 / 3.0, 1e-6);
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

// Open Michaelis-Menten network: inflow v0, full mass action binding and
// catalysis, first-order product removal. Total enzyme Et is conserved, so the
// steady state is S* = Km v0 / (kc Et - v0), Km = (kr + kc) / kf, ES* = v0 / kc,
// P* = v0 / kd, independent of the initial state on the Et leaf.
void michaelis_menten_steady_state() {
  const double v0 = 0.3, kf = 2.0, kr = 1.0, kc = 0.5, kd = 0.2, et = 1.0;
  Model m;
  m.name = "open_mm";
  m.compartment("cell", 1.0)
      .species("S", "cell", 4.0)
      .species("E", "cell", 0.6)
      .species("ES", "cell", 0.4)
      .species("P", "cell", 0.0)
      .param("v0", v0)
      .param("kf", kf)
      .param("kr", kr)
      .param("kc", kc)
      .param("kd", kd)
      .reaction("inflow", " -> S", "v0")
      .reaction("bind", "S + E -> ES", "kf*S*E")
      .reversible("bind", "kr*ES")
      .reaction("cat", "ES -> E + P", "kc*ES")
      .reaction("removal", "P ->", "kd*P");
  const double km = (kr + kc) / kf;
  Result r = m.steady_state();
  near(r.final("S"), km * v0 / (kc * et - v0), 1e-6);
  near(r.final("ES"), v0 / kc, 1e-6);
  near(r.final("E"), et - v0 / kc, 1e-6);
  near(r.final("P"), v0 / kd, 1e-6);
}

// Kinetic laws are extents (amount/time); a concentration species in a
// compartment of volume V changes by rate/V. A law written as k*A*V therefore
// gives dA/dt = -k A, and the amount-based law k*A gives dA/dt = -k A / V.
void compartment_volume_scaling() {
  Model m;
  m.name = "volume";
  m.compartment("big", 4.0)
      .species("A", "big", 1.0)
      .species("C", "big", 1.0)
      .param("k", 0.5)
      .reaction("conc", "A ->", "k*A*big")
      .reaction("amount", "C ->", "k*C");
  Result r = m.simulate(0.0, 2.0, 3);
  near(r.final("A"), std::exp(-1.0), 1e-6);
  near(r.final("C"), std::exp(-0.25), 1e-6);
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
  // Hairer & Wanner reference solution at t = 40.
  near(r.final("A"), 0.7158270687, 1e-6);
  near(r.final("B"), 9.185534765e-6, 1e-9);
  near(r.final("C"), 0.2841637457, 1e-6);
  near(r.final("A") + r.final("B") + r.final("C"), 1.0, 1e-9);
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
  michaelis_menten_steady_state();
  compartment_volume_scaling();
  ssa_poisson_moments();
  std::cout << checks << " scientific checks passed\n";
  return 0;
}
