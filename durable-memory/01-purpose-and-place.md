# Purpose and place

lmbio is the biochemical host in the scripted scientific library family. It exposes one `bio` context to lmscript DSL v5. Reaction models, simulations and named results are host objects with builder calls. One executable embeds its schema, notices, SUNDIALS and XML parser; no Python or data install is needed.

The implementation follows simulation/PROCEDURE.md and Recommendation 1 in FEASIBILITY.md, refined after checking actual licenses. The host is MIT. libSBML (LGPL) and the LLVM-heavy RoadRunner stack are excluded. lmbio is an explicitly bounded SBML-lite implementation, not a complete replacement for an SBML conformance library. Results export as CSV for lmplot.
