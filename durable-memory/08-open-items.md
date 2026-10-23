# Scope beyond v0.1

- Full SBML conformance: packages, events, delays, algebraic rules, initial assignments, user-defined functions and broader MathML.
- Dynamic compartments and unit conversion/dimensional validation.
- CVODES forward/adjoint sensitivities instead of finite differences.
- Sparse networks, tau leaping, spatial/diffusion models and stochastic time-dependent propensities.
- Mass-action SSA propensities exist only through `mass_action` (0.1.1); hand-written rates are still used as given, and SBML export drops the stored propensity.
- Model objects/results stay alive until host exit; long scans should bound their size.

The original fixtures are permissively licensed; the LGPL upstream SBML Test Suite is not shipped or represented as passed.

## Open after the 2026-10-23 review

1. The 0.1.1 API (mass_action, set_state, Result.append/integral/crossing/min, compartment get/set) ships in v0.1.1.
2. SSA trajectories for one seed differ across platforms' libm; examples assert statistics with tolerances.
3. Events, initial assignments, function definitions and piecewise are still rejected in SBML.
4. Sensitivities are finite differences, not CVODES.
