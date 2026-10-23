# Examples and test oracles

examples/biochemical_network.lms models substrate/enzyme binding, dissociation and catalytic product release. It checks substrate and enzyme conservation and exports concentration trajectories. examples/stochastic.lms exports an immigration-death trajectory from a fixed seed; reruns must be byte-identical.

The DSL tests separately exercise A->B decay, A<->B equilibrium, enzyme conservation, Lotka-Volterra's conserved quantity, parameter scans, assignment/rate rules, finite-difference sensitivity, SSA determinism and SBML save/load. Original fixtures cover fixed volumes, initial amounts, boundary species, local kinetic parameter shadowing, rules and prefixed XML namespaces.

Closed forms are independent oracles: first-order A=A0*exp(-k*t), reversible B/A=kf/kr with total conserved, immigration-death Poisson mean lambda=(birth/death)*(1-exp(-death*t)) from zero counts. Native SSA tests compare empirical mean and variance from 2000 seeds against lambda with three standard errors. The Robertson stiff network uses the standard three-species equations with A(40) approximately 0.715827; see the [SUNDIALS CVODE Roberts example](https://github.com/LLNL/sundials/blob/v7.2.1/examples/cvode/serial/cvRoberts_dns.c).

The expression language accepts scientific notation inside strings, while DSL literals require decimal spelling. Use bio.assert_close for scalar numeric tests and bio.assert_true for predicates. All example savedsl outputs are under build/; result CSV files are under build/examples/. Reversible(id,rate) takes the ID of the existing forward reaction.
