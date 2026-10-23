# Scope beyond v0.1

- Full SBML conformance: packages, events, delays, algebraic rules, initial assignments, user-defined functions and broader MathML.
- Dynamic compartments and unit conversion/dimensional validation.
- CVODES forward/adjoint sensitivities instead of finite differences.
- Sparse networks, tau leaping, spatial/diffusion models and stochastic time-dependent propensities.
- Automatic inference of mass-action combinatorial stochastic propensities from deterministic rates.
- Model objects/results stay alive until host exit; long scans should bound their size.

The original fixtures are permissively licensed; the LGPL upstream SBML Test Suite is not shipped or represented as passed.
