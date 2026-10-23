# Numerical methods

Reaction extents map through stoichiometry to concentration or amount derivatives. CVODE BDF handles stiff kinetics with dense linear solves and user tolerances. KINSOL provides nonlinear steady-state solving; integration supplies a fallback for conservation-related singular systems. Steady-state output is accepted only after a derivative residual check.

Gillespie direct simulation is original code: channel propensities determine an exponentially distributed next firing time and a weighted channel choice. A pending firing survives output sample boundaries, so changing the requested sampling grid does not change the underlying path. Forward and reverse channels are sampled separately. Output points sample the right-continuous state between firings; counts remain integral.

Local sensitivities use centered finite differences around the selected parameter with an absolute perturbation based on the requested relative step and parameter scale. Solver tolerances must resolve that perturbation. Simulations return values without mutating initial models. Bound work and matrix limits prevent unbounded allocations or event loops. No native exception crosses a SUNDIALS C callback.
