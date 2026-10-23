# Resource limits

Why: Model and Result objects live in arenas until exit, so a DSL loop that keeps simulating grows
without bound (each Result may hold up to 1e7 doubles, 80 MB, and the arena allows 100000 of
them). On 2026-10-23 a runaway lm* host (lmnum) reached 16 GB RSS and filled the 24 GB machine;
every host now has a total memory budget.

- **Total memory budget**, default 4096 MiB, set with `LMBIO_MAX_MEMORY_MB` (positive whole MiB;
  anything else exits 2 with the usage text). Shown in the usage message. Configured in `main`
  only for `--dsl` runs.
- `cpp/memory_budget.{hpp,cpp}` (identical copy in every lm* host) replaces the global
  `operator new`/`delete` and counts block sizes from the C allocator. It is compiled into
  `bio_core`, so the ctest executables use it too.
- Counted through operator new: models, compiled networks and expressions, Result tables (now
  reserved at their exact size), SSA trajectories (`out` rows; events stay capped at 1e7), VM
  strings.
- Counted outside operator new:
  - SUNDIALS (malloc): `SolverResources(q)` charges `(2 q^2 + 32 q) * 8` bytes + 64 KiB (dense
    Jacobian, the saved copy CVLS keeps, pivots and state vectors) before CVODE/KINSOL are
    created and releases it in its destructor. With the 2048-state cap that is at most ~64 MiB.
  - pugixml: `load_sbml`/`save_sbml` install `pugi::set_memory_management_functions` with
    wrappers over the counted operator new, so the whole-file buffer and the node tree count.
    pugixml wants nullptr on failure: the wrapper keeps the `Exceeded` and it is rethrown right
    after `load_file`, and before `save_file` (a failed append would otherwise write a truncated
    document).
- CVODE/KINSOL callbacks (`rhs`, `kin_rhs`) never let an exception into the C frames: they
  already caught `std::exception`; now they also catch `...`, store an `Exceeded` (no heap state)
  in the callback context, copy the message without letting a second allocation failure escape,
  and the solver caller rethrows the `Exceeded` after `CVode`/`KINSol` return.
- Exceeding the budget throws `membudget::Exceeded` (a `std::bad_alloc`); host methods turn it
  into a `dslvm::Error` with the same text and `main` also catches `std::exception` around
  `Program::load`/`run`. The first failure prints
  `lmbio: memory budget exceeded: ... (set LMBIO_MAX_MEMORY_MB to raise it)` on stderr; the JSON
  line carries the same text; exit 3.
- Not covered: SUNDIALS context/CVODE bookkeeping beyond the estimate (a few KiB), temporaries of
  one call (bounded by the per-call caps: 2048 states, 1e7 result cells, 1e7 SSA events).
  No `setrlimit` (not portable to macOS static builds).
- Measured (x86_64 Linux, WSL): the test.sh runaway (`simulate(0, 0, 9990)` on a 1001-column model,
  80 MB per Result, solver skipped) stops at 64 MiB in 0.14 s (68 MB peak RSS) and at the default
  4096 MiB after 11.9 s with 4.15 GB peak RSS (tracked 4045 MiB). Before `reserve`, push_back
  slack made it 23.5 s and 2.6 GB RSS for the same tracked count. A 2000-species `simulate` peaks at
  66 MB RSS against a 62 MiB charge. test.sh also checks the SUNDIALS charge (32 MiB budget) and a
  24 MiB SBML file (16 MiB budget); the default-budget run only on Linux, under `ulimit -v 6291456`.
