# Self-contained autorun gallery

README gallery captions and every examples listing provide copy-paste `lmctl autorun` raw-GitHub commands. Each examples/*.lms contains both the scientific computation and the lmplot or lmdraw block, with cli/install bindings for both. After savedsl it executes each host and asserts success. Generated CSV/bytecode/frames are under the current directory's build/; the PNG is images/<example>.png and its path is printed. No repository-relative input, shell helper, renderer PATH setup or VISUALS_NETWORK flag is needed. The former examples/visuals scripts and render-one helper were removed after inlining.

For checkout maintenance, examples/render-all.sh executes the same self-contained programs and copies their PNGs (and GIFs, when the example wrote one) into docs/images/. Animated examples (decay_clock, repressilator) call ffmpeg themselves when `which("ffmpeg")` finds it and print SKIP otherwise. Missing ffmpeg reports an explicit animation skip; PNG generation always runs through the declared renderer binding. Root images/ is ignored. Gallery validation checks autorun URL ownership/source existence, inline renderer bindings, output paths and existing PNG/GIF signatures, dimensions and size budgets. All existing physics, determinism, schema and CLI checks remain in test.sh.

Remote autorun verification uses a separate empty temporary working directory for each URL, a 300-second timeout, no scientific host or renderer on PATH, and compiler-managed downloads. The expected images/<example>.png must exist with a valid PNG signature after each command succeeds.

## Verified raw URLs (2026-10-23)

Each command ran successfully through `lmctl autorun` with `timeout 300` in its own initially empty directory. PATH contained lmctl/lmscript and system tools, with neither lmbio nor lmplot available; a fresh shared compiler cache installed the released hosts. Every output was a valid 900x520 PNG, byte-identical to the committed gallery image.

- `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmbio/main/examples/biochemical_network.lms` — exit 0, 4.02 seconds, `images/biochemical_network.png` (33554 bytes).
- `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmbio/main/examples/decay_clock.lms` — exit 0, 1.82 seconds, `images/decay_clock.png` (39003 bytes).
- `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmbio/main/examples/robertson_switch.lms` — exit 0, 1.52 seconds, `images/robertson_switch.png` (32579 bytes).

## 2026-10-23 review and enhancement

18 examples, 20 images (18 PNG at 900x520 or 960x540/560, 2 GIF). The README gallery is grouped by theme (gene circuits and noise; enzymes, metabolism and chemistry; medicine, epidemics and populations) with a title, a one-sentence result and the autorun command under every picture. All images were opened and checked for clipped labels and overlapping keys. Fixes to the earlier gallery: legends sat above the titles (now inside the plot), Robertson's title promised a fast intermediate the plot did not show (now log time with B x 1e4), logistic x range ran past the data, reversible curves were coarse. Regenerating with lmctl 0.2.369 (the CI compiler) and 0.2.401 gave byte-identical PNG/GIF files.
Local autorun note: a stale ~/.local/bin/lmbio (an unrelated 0.1.0 binary without --lmcode-schema) shadowed the release; autorun checks therefore ran with a PATH that excludes it, and the README says how to recognise the error.
