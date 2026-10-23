# Examples and test oracles

examples/biochemical_network.lms models substrate/enzyme binding, dissociation and catalytic product release. It checks substrate and enzyme conservation and exports concentration trajectories. examples/stochastic.lms exports an immigration-death trajectory from a fixed seed; reruns must be byte-identical.

The DSL tests separately exercise A->B decay, A<->B equilibrium, enzyme conservation, Lotka-Volterra's conserved quantity, parameter scans, assignment/rate rules, finite-difference sensitivity, SSA determinism and SBML save/load. Original fixtures cover fixed volumes, initial amounts, boundary species, local kinetic parameter shadowing, rules and prefixed XML namespaces.

Closed forms are independent oracles: first-order A=A0*exp(-k*t), reversible B/A=kf/kr with total conserved, immigration-death Poisson mean lambda=(birth/death)*(1-exp(-death*t)) from zero counts. Native SSA tests compare empirical mean and variance from 2000 seeds against lambda with three standard errors. The Robertson stiff network uses the standard three-species equations with A(40) approximately 0.715827; see the [SUNDIALS CVODE Roberts example](https://github.com/LLNL/sundials/blob/v7.2.1/examples/cvode/serial/cvRoberts_dns.c).

The expression language accepts scientific notation inside strings, while DSL literals require decimal spelling. Use bio.assert_close for scalar numeric tests and bio.assert_true for predicates. All example savedsl outputs are under build/; result CSV files are under build/examples/. Reversible(id,rate) takes the ID of the existing forward reaction.

## Gallery examples (18)

Each example is self-contained (`lmctl autorun <raw url>` from an empty directory), computes with lmbio, asserts a known answer inside the lmbio block, then draws with lmplot (gnuplot) or lmdraw. Numbers are passed from the lmbio block to the drawing step through `bio.print("row,...")` lines that the outer script filters from `r.stdout` into CSV, or through CSV files under build/examples/.

- toggle_switch (lmdraw): Gardner bistable switch, a = 10, n = 2; steady state solves both nullclines, exact saddle (2,2); 22 starts classified. Circuit diagram + phase portrait drawn by lmdraw from the CSVs.
- repressilator (lmdraw, GIF): Elowitz-Leibler alpha 216, alpha0 0.216, beta 5, n 2; period 8.69 +- 0.15 from upward crossings; n = 1 damps (spread < 0.01). 60 lmdraw frames, ring node shade = protein level; ffmpeg GIF when available.
- negative_autoregulation: same steady level 1; simple t1/2 = ln 2; strong-repression limit x^3 = 1 - exp(-3t) gives t1/2 = ln(8/7)/3 = 0.0445.
- goodwin_threshold: three-stage Goodwin loop with a = 10; secant condition gain n z^n/(1+z^n) = 8 gives n_c = 9.13; amplitude zero below n_c - 0.5, positive above n_c + 0.5.
- gene_noise: birth-death Fano 1 (time average of a 5000-unit SSA run) vs two-stage bursty expression Fano 1 + b gm/(gm+gp) = 5.55.
- enzyme_saturation: full mass-action mechanism; v0 = kcat max(ES) at nine S0 within 3% of Vmax S/(Km+S), Km = (koff+kcat)/kon = 1.
- feedback_inhibition (lmdraw): branched pathway with end-product inhibition; steady state equals the closed-form quadratic; flux maps with arrow width ~ flux.
- parameter_sensitivity: scaled sensitivities of P to kcat/kon/koff equal 1 - kcat/(kon(Km+S)), Km/(Km+S), -koff/(kon(Km+S)) early (0.75/0.5/-0.25) and vanish at completion.
- pk_dosing: oral one-compartment model, 400 mg q12h vs 800 mg q24h; AUC (rate rule) over the last interval = dose/CL; Css,avg identical; only q12h stays in 3-12 mg/L. Doses restart runs by setting species from the previous run's final row (works with the 0.1.0 API).
- sir_epidemic: final size S_inf = S0 exp(-R0 (1 - S_inf)) by fixed-point iteration for eight R0; attack rate exceeds 1 - 1/R0.
- robertson_switch: decade segments 1e-6..1e5 on a log axis; B peak 3.65e-5; A(40) = 0.715827.
- earlier examples: biochemical_network, stochastic, decay_clock (GIF), reversible_balance, predator_prey, cascade_delay, logistic_limit.

DSL notes learned while writing them: DSL literals interpolated with `${x}` keep their lmscript type, so an integral float becomes an int; the lmdraw examples pass pixel values through `F(x) = floor(x) + 0.5`. lmdraw only writes under build/rendered/ (create it first) and caps path data at 16384 bytes (decimate trajectories). The DSL has no exp/log built-ins; use pow(e, x). A `_dsl_` block may sit inside an outer loop (its `end _dsl_` still starts at column 1) to render one frame per iteration. In gnuplot multiplot panels, `set key ... Right noreverse` places a right-side key reliably; `Left reverse` with `right` did not.
