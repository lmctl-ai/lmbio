# lmbio

Biochemical reaction networks and SBML-lite for lmscript in one static executable. Build models with chained calls or load SBML Level 3 core XML, integrate stiff kinetics with CVODE BDF, find steady states with KINSOL and integration fallback, run seeded Gillespie simulations, and export CSV for [lmplot](https://github.com/lmctl-ai/lmplot).

[![CI](https://github.com/lmctl-ai/lmbio/actions/workflows/ci.yml/badge.svg)](https://github.com/lmctl-ai/lmbio/actions/workflows/ci.yml)

```lmscript
p = _dsl_ cli="lmbio" install="github:lmctl-ai/lmbio@latest";
let m = bio.model("conversion").compartment("cell", 1.0);
m.species("A", "cell", 1.0).species("B", "cell", 0.0);
m.param("k", 0.7).reaction("convert", "A -> B", "k*A");
let result = m.simulate(0.0, 4.0, 41);
bio.assert_close(result.final("A"), 0.060810062625, 0.000001);
bio.assert_close(result.final("A") + result.final("B"), 1.0, 0.000001);
result.save_csv("conversion.csv");
end _dsl_
savedsl(p, "conversion.dslb");
system ${dsl_cli(p)} --dsl conversion.dslb;
```

Run with `lmscript conversion.lms`. lmscript finds the host on PATH or installs its release binary. If another application named lmbio is on PATH, select this host first; it must answer `lmbio --dsl-schema --json`. This is a scripting host: its CLI accepts `--dsl FILE...`, `--dry-run --dsl FILE...`, `--version`, `--notices`, `--dsl-schema [--json]`, and `--dsl-signatures`. Usage errors exit 2; bytecode load/run errors exit 3. Dry run checks bytecode without running the model or writing files.

| API | Purpose |
| --- | --- |
| `bio.model(name)`, `bio.load_sbml(path)` | create or import a model |
| `model.compartment(id,size)` | fixed, positive compartment volume |
| `model.species(id,compartment,initial)` | initial concentration |
| `model.param(id,value)` | kinetic parameter |
| `model.reaction(id,"A + B -> 2 C","k*A*B")` | stoichiometry and rate expression; empty sides support sources/sinks |
| `model.reversible(id,"kr*C")` | add reverse rate to an existing reaction |
| `model.rule(id,"expression")` | assignment rule |
| `model.rate_rule(id,"expression")` | differential rule |
| `model.set(id,value)`, `model.get(id)` | change/query initial species or parameter values; useful in parameter scans |
| `model.tol(rtol,atol)`, `model.validate()` | configure error control and validate the network |
| `model.simulate(t0,t1,points)` | CVODE BDF time course |
| `model.steady_state()` | steady state with residual verification |
| `model.ssa(t1,points,seed)` | Gillespie direct simulation with an integer seed |
| `model.sensitivity(parameter,t0,t1,points,relative_step)` | central finite-difference derivatives |
| `model.save_sbml(path)` | write supported semantics as SBML L3V2 MathML |
| `result.value(id,row)`, `result.final(id)` | named values, zero-based rows |
| `result.rows/cols/get/column` | matrix dimensions, indexed values, column names |
| `result.max(id)`, `result.time_of_max(id)` | sampled maxima and their first time |
| `result.save_csv(path)` | comma-separated matrix with header |
| `bio.assert_close(a,b,absolute_tolerance)`, `bio.assert_true(bool)` | script assertions |

Results contain `time`, species in creation order, then parameters in identifier order. Simulation does not change initial model values. CSV is the exchange format for other hosts. The DSL has no list/map/callback values; kinetic laws and rules are expression strings. Expressions support arithmetic, powers, scientific notation, and common elementary functions (`exp`, `log`, `sin`, `sqrt`, and others). DSL numeric literals themselves use decimal notation.

## Model semantics and limits

Kinetic laws are reaction extents (substance per time). A concentration species receives stoichiometry × rate / compartment volume. SBML `hasOnlySubstanceUnits` species use amounts; initial amount/concentration attributes are converted using the compartment volume. Boundary and constant species do not change through reactions. Local kinetic parameters shadow global symbols within their own reaction. Assignment rules are evaluated in dependency order; cycles and unknown names are errors. Rate rules and assignment rules are supported for species and parameters, with conflicting definitions rejected.

The reader supports SBML L3V1/V2 **within the documented subset**, rather than claiming complete SBML conformance. It handles fixed compartments, species, parameters, reactions, local parameters, arithmetic MathML and assignment/rate rules. Unsupported semantics are reported explicitly. Units are descriptive and are not converted or dimensionally checked. Packages, events, delays, algebraic rules, initial assignments, function definitions, dynamic compartments and conversion factors are outside v0.1. See [schema and semantics](durable-memory/03-schema-v0.md).

SSA interprets species values as molecule counts in unit-volume compartments and rate expressions as propensities. Initial counts and stoichiometric coefficients must be nonnegative integers. Users supply combinatorial propensities for higher-order reactions, for example `k*X*(X-1)/2` for a dimerization channel. SSA has no assignment/rate rules or explicit time-dependent propensities. Reusing a seed produces byte-identical output on the same platform; an explicit uniform mapping from `mt19937_64` avoids implementation-defined distribution algorithms.

Sensitivities are derivatives of named output values with respect to the selected initial parameter, not normalized elasticities. The time column stays time. Choose a step large enough to dominate solver error, or tighten `tol()` before differentiating. Steady-state and sensitivity results depend on the supplied model and tolerances.

## Build and tests

```sh
scripts/sync-deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
./test.sh
```

Requires CMake 3.24+, C++17, git, Python 3, and lmscript for tests (`npm i -g @lmctl-ai/lmctl@0.2.369`). Linux releases are fully static; macOS uses only system libraries; Windows statically links MinGW runtimes. No external runtime data is required.

The tests assert first-order decay, reversible equilibrium, mass-action enzyme conservation, Lotka–Volterra invariants, Robertson stiff kinetics, finite-difference sensitivities, stochastic Poisson moments, and SBML round trips. They also check schema/registry/compiler parity, dependency pins, static linking, deterministic CSV, dry run and error codes. All fixtures are original MIT-licensed test data. The upstream SBML Test Suite is LGPL and is not copied into this project; see [license decisions](durable-memory/02-libraries-and-licenses.md).

## License

Our host and reaction code are [MIT](LICENSE). SUNDIALS 7.2.1 is BSD-3-Clause and pugixml 1.15 is MIT. Their pinned sources live only in ignored `third_party/`. Full notices are embedded in the binary (`lmbio --notices`) and supplied in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). There is no libSBML, RoadRunner, LLVM, GPL or LGPL scientific dependency.
