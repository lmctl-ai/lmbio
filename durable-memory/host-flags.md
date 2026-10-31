# Host flag cutover

The parser/callers accept only --lmcode, --lmcode-schema and --lmcode-signatures.
The SDK comes through sync-sdk.sh at 20325f7; source compiler probes and standard
signature parity use the actual v6/206-member capabilities rather than package
version. DSLB magic, .dslb paths, biochemical models and output semantics remain.

The complete Linux gate passes 20 checks with private updated plot/draw hosts:
native biochemical CTest, exact metadata, model examples and deterministic output,
19 failure cases, dry-run, memory accounting and gallery assets. The gallery gate
now derives its URL from canonical repository identity instead of local checkout
basename; an arbitrary-clone test passes and a wrong-owner control still fails.
The baseline arbitrary-name checkout exposed this pre-existing harness defect.
