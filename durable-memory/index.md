# lmbio durable memory

`lmbio` is a static, dependency-free lmscript `_dsl_` host for systems biology, chemical reaction kinetics, ODE/SSA simulation, and SBML model exchange.
Repository: https://github.com/lmctl-ai/lmbio.

## 60-second takeover

```sh
scripts/sync-deps.sh                  # Sync pinned third-party dependencies
cmake -S . -B build                   # Configure CMake build
cmake --build build --parallel 4      # Build binary at build/lmbio
./test.sh                             # Run full test suite (pins, native suites, DSL programs)
```

## Document map

| File | Contents |
| --- | --- |
| [01-purpose-and-place.md](01-purpose-and-place.md) | Architectural role, capabilities, and simulation host scope |
| [02-libraries-and-licenses.md](02-libraries-and-licenses.md) | Third-party libraries, licensing, and clean-room implementation rules |
| [03-schema-v0.md](03-schema-v0.md) | Host DSL schema, types, functions, and models |
| [04-numerics.md](04-numerics.md) | Deterministic ODE solvers, stochastic simulation algorithm (SSA), numerical tolerances |
| [05-build-run-test.md](05-build-run-test.md) | Toolchain requirements, build commands, test suite verification |
| [06-ci.md](06-ci.md) | CI matrix (5 platforms), automated releases, tag workflows |
| [07-examples.md](07-examples.md) | DSL examples, kinetics benchmarks, and regression suites |
| [08-open-items.md](08-open-items.md) | Roadmap, planned extensions, and known limitations |
| [09-visual-gallery.md](09-visual-gallery.md) | Plot rendering, visual verification, and gallery examples |
| [10-resource-limits.md](10-resource-limits.md) | Execution time, memory budget, and resource boundary limits |

## SDK pin and sync

The vendored SDK header is pinned via `sdk.lock` pointing to `https://github.com/lmctl-ai/lmcode-cpp`:
```sh
scripts/sync-sdk.sh                   # Refetches pinned cpp/dslvm.hpp and verifies sha256
```

## Adding an example or function

### Adding an example
1. Create `examples/<name>.lms` using `_dsl_ cli="lmbio" install="github:lmctl-ai/lmbio@latest"`.
2. Verify execution: `build/lmbio run examples/<name>.lms` or via `lmscript`.
3. Add the example to `./test.sh` test run list.
4. Add any visual output to `docs/images/<name>.png` and update `README.md` with demo table and `lmctl autorun` command.

### Adding a host function
1. Define the function/method in `schema/lmbio.dsl.json`.
2. Register and implement the function in C++ source under `src/` using `dslvm::Registry`.
3. Rebuild and verify schema synchronization with `cmake --build build` and `./test.sh`.

## Releases

Releases are triggered automatically on push of a `v*` tag (or main workflow depending on repo config). The release workflow builds across Linux x64/arm64, macOS x64/arm64, and Windows x64, publishing 5 binaries with `SHA256SUMS`, `LICENSE`, and `THIRD_PARTY_NOTICES`.
