# Libraries and licenses

| Component | Pin | License |
| --- | --- | --- |
| SUNDIALS 7.2.1 | `2dcb3e018b4c4cfe824bff09eb52184ed083e368` | BSD-3-Clause; LICENSE + NOTICE embedded |
| pugixml 1.15 | `ee86beb30e4973f5feffe3ce63bfa4fbadf72f38` | MIT |
| lmcode-cpp | `sdk.lock` commit and SHA-256 | MIT |
| adapted lmnum expressions and own reaction/SBML/SSA code | repository sources | MIT |

`deps.lock` and `scripts/sync-deps.sh` fetch scientific dependencies into ignored `third_party/`. No dependency source is committed except the small single-header scripting SDK. CVODE and KINSOL use serial vectors and dense linear algebra; MPI, OpenMP, BLAS, LAPACK and other solver packages are disabled. Eigen is unnecessary, so no MPL dependency is added.

The feasibility document incorrectly described the SBML Test Suite cases as BSD-ish. The [actual upstream license](https://github.com/sbmlteam/sbml-test-suite/blob/develop/LICENSE.txt) is LGPL-2.1-or-later, including distributed semantic cases. The user's explicit no-GPL/LGPL restriction takes precedence: no test-suite files are copied, fetched, embedded or shipped. Original fixtures instead exercise supported semantics and compare closed-form or independent published numerical values. This is not a claim of passing the upstream SBML conformance suite.

[Upstream SUNDIALS](https://github.com/LLNL/sundials/tree/v7.2.1) and [pugixml licensing](https://pugixml.org/license.html) establish the scientific dependency terms. Full pinned source notices and the SDK MIT text are in THIRD_PARTY_NOTICES and embedded in --notices. Standard platform compiler runtimes retain their applicable system and runtime-exception terms.
