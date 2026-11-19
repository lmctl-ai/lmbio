# Bounded Biology Worker (R48 phase 2)

A separate optional build target for three reviewed teaching networks; it does not
embed/distribute the LMCode browser compiler or expose the CLI/DSL, paths, SBML,
user expressions, external references, or network imports. It is not a general
reaction-model sandbox. The two phase-2 books currently use the Python exact /
bounded RK4 path; this adapter is an independently tested extension point.

Build using the pinned third_party sources, Emscripten 7.0.0 and its notices:

```sh
scripts/sync-deps.sh
# source your emsdk_env.sh first
emcmake cmake -S . -B /tmp/lmbio-browser -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build /tmp/lmbio-browser --target bio_worker --parallel 4
node browser/numerical-test.mjs /tmp/lmbio-browser
flock /tmp/browser-gate.lock node browser/browser-test.mjs /tmp/lmbio-browser /tmp/lmbio-browser-check.json
```

Serve `bio_worker.mjs`, `bio_worker.wasm`, `worker.mjs` and `client.mjs` from one
immutable same-origin directory. Serve WASM as application/wasm and preserve MIT
host, BSD-3-Clause SUNDIALS, pugixml and Emscripten/SDK notices. No production
deployment is part of this change. Call `runBio(request, {signal, timeout})` from
the module client; each call creates and always terminates one module Worker.
Default timeout is 5 seconds; permitted caller timeouts are 1..10000 ms. Cancel
terminates the Worker, including synchronous solver code. A later request starts
with a fresh module, model, memory budget and RNG.

Request has integer nonnegative id, model, parameters, horizon, points, optional
uint32 seed (default 0), and optional boolean tighter. Unknown fields are errors.

| model | parameters | output columns | engine |
|---|---|---|---|
| decay | initial A [0,100], k [0,10] | time,A,B | CVODE BDF |
| enzyme | initial E,S [0,100], kon,koff,kcat [0,10], initial ES [0,100] | time,E,S,ES,P | CVODE BDF |
| birth-death | initial N integer [0,100], birth,death [0,10] | time,N | Gillespie SSA |

All numbers must be finite. Horizon is (0,20], points integer 2..257. Other initial
species are zero and compartment volume is one. Enzyme mechanism is
E+S -> ES, ES -> E+S, ES -> E+P, with the corresponding mass-action rates.
Birth/death propensities are birth and death*N. CVODE tolerances are 1e-8/1e-10
normally and 1e-11/1e-13 when tighter=true. No steady-state/KINSOL endpoint is
exposed. SSA uses the seed directly; repeated paths match for the same binary,
not as a scientific requirement across platforms/engine versions. Ensemble
checks use the birth-death analytic mean, not a memorized seed path.

Successful response has matching id, ok:true, engine, columns, rows, and a
transferred Float64Array values (row-major, including initial time). Errors have
ok:false and at most 256 characters of diagnostic text. Never consume an old
buffer after an error: width resets to zero. The C ABI repeats validation before
constructing a model and copies only finite, nonnegative-with-roundoff outputs
into a static buffer of 257*5 doubles. It never exports a filesystem handle.

Wasm linear memory starts at 32 MiB and grows only up to 512 MiB. The allocator's
pre-main budget is 256 MiB on 32-bit hosts, retaining the 4096 MiB native 64-bit
default. The one-time 64 MiB diagnostic reserve remains below the wasm ceiling.
This fixes the previous pre-main 4096 MiB -> size_t zero overflow. C++ exception
catching is explicitly enabled at compilation and linking so invalid requests
become diagnostics rather than unhandled CppException values.

Numerical tests cover analytic decay, conservation at every sample, tighter
CVODE convergence, the independently recorded enzyme endpoint, seeded SSA
reproducibility, integer nonnegative counts and a 256-seed analytic moment check.
The real-browser test checks both 1280 and 390, request validation, repeated
seeds, cancellation, hard timeout and same-origin-only asset loading. These
checks do not license arbitrary networks, external SBML or clinical prediction.
