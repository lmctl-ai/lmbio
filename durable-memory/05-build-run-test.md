# Build, run and measured verification

2026-10-23, x86_64 Linux, GCC 11 and Clang with CMake/Ninja. A clean Clang configure/build/native-test run took 21.86 seconds with four build jobs. The stripped Release binary links all third-party libraries and `libstdc++`/`libgcc` statically and links only dynamic glibc (`>= 2.35`, verified via `readelf -d` allowlist). No Eigen, JIT, libSBML or runtime data is needed.

Run scripts/sync-deps.sh, cmake -S . -B build, cmake --build build --parallel 4, then ./test.sh. Dependency verification checks pinned commit and clean tracked source; SDK verification checks its content hash. The compiler selector probes emitted bytecode v5 rather than relying only on a version string. Tests put build/ first on PATH to select this host even if another application named lmbio is installed.

- 19 grouped integration checks: pins, build, native suites, exact version, generated/embedded/registry/compiler schema parity, notices, runtime linkage allowlist, all programs, deterministic reruns, error paths, dry run, CLI codes, memory budget and gallery.
- Three native suites: 184 biochemical core checks, 26 XML/MathML checks and 12 scientific checks.
- Eleven DSL programs (nine tests, two examples) compile and run with assertions. Both deterministic kinetics and seeded stochastic CSV files are byte-identical on rerun.
- Fourteen explicit runtime error cases check exit 3 and explanatory text; usage combinations check exit 2.
- Scientific checks cover closed forms, reversible equilibrium with conserved totals, Michaelis-Menten mass-action conservation, Lotka-Volterra invariants, Robertson A(40) approximately 0.715827, sensitivity including a zero-valued parameter, and 2000 SSA immigration-death realizations within three standard errors of Poisson moments.
- Native tests are always active under NDEBUG. AddressSanitizer/UndefinedBehaviorSanitizer and leak detection pass. Clang static analysis of model/SBML sources, shellcheck and Python compilation pass.

Tests and examples are original MIT material. The GPL/LGPL upstream SBML conformance suite is not fetched or shipped, and passing it is not claimed. tests/fixtures are authored XML models; generated XML and bytecode use build/ or temporary directories. Zero-duration deterministic trajectories, output-grid-independent stochastic paths, unsupported MathML, namespaces and failed solver calls have regressions.
