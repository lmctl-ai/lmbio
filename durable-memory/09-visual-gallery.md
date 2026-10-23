# Self-contained autorun gallery

README gallery captions and every examples listing provide copy-paste `lmctl autorun` raw-GitHub commands. Each examples/*.lms contains both the scientific computation and the lmplot block, with cli/install bindings for both. After savedsl it executes each host and asserts success. Generated CSV/bytecode/frames are under the current directory's build/; the PNG is images/<example>.png and its path is printed. No repository-relative input, shell helper, renderer PATH setup or VISUALS_NETWORK flag is needed. The former examples/visuals scripts and render-one helper were removed after inlining.

For checkout maintenance, examples/render-all.sh executes the same self-contained programs, copies their PNGs into docs/images/, and uses ffmpeg on the generated frames to recreate the committed GIF. Missing ffmpeg reports an explicit animation skip; PNG generation always runs through the declared renderer binding. Root images/ is ignored. Gallery validation checks autorun URL ownership/source existence, inline renderer bindings, output paths and existing PNG/GIF signatures, dimensions and size budgets. All existing physics, determinism, schema and CLI checks remain in test.sh.

Remote autorun verification uses a separate empty temporary working directory for each URL, a 300-second timeout, no scientific host or renderer on PATH, and compiler-managed downloads. The expected images/<example>.png must exist with a valid PNG signature after each command succeeds.

## Verified raw URLs (2026-10-23)

Each command ran successfully through `lmctl autorun` with `timeout 300` in its own initially empty directory. PATH contained lmctl/lmscript and system tools, with neither lmbio nor lmplot available; a fresh shared compiler cache installed the released hosts. Every output was a valid 900x520 PNG, byte-identical to the committed gallery image.

- `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmbio/main/examples/biochemical_network.lms` — exit 0, 4.02 seconds, `images/biochemical_network.png` (33554 bytes).
- `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmbio/main/examples/decay_clock.lms` — exit 0, 1.82 seconds, `images/decay_clock.png` (39003 bytes).
- `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmbio/main/examples/robertson_switch.lms` — exit 0, 1.52 seconds, `images/robertson_switch.png` (32579 bytes).
