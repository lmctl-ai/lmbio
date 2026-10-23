# Reproducible visual gallery

README starts with six picture cards and links to every story. Each examples/*.lms computes with this host, asserts its numerical result, writes CSV under build/examples/, then invokes its companion examples/visuals/*.lms. The companion binds lmplot through cli/install and renders a 900x520 PNG under docs/images/. No data is fabricated by the renderer. The animated story renders 24 frames; ffmpeg encodes a compact GIF with a moving time cursor across the computed trajectory.

Run examples/render-all.sh from the checkout (it changes to the repo root). Put this host's build and lmplot on PATH, or set VISUALS_NETWORK=1 with GITHUB_TOKEN for compiler-managed renderer installation. Without lmplot/network the examples explicitly report SKIP and still compute/assert. Without ffmpeg PNGs regenerate and animation reports SKIP. Generated CSV, frames and bytecode remain ignored under build/. Committed images need no renderer to view.

Tests run every computational example; when renderer available they regenerate figures too. check-gallery.py checks README example/image links, PNG/GIF magic, PNG dimensions, and 300 KB/3 MB budgets. Images are visually opened before commit. Renderer programs use a consistent palette and descriptive scientific axes. No library behavior or linked dependencies change. All example scripts and generated figures are MIT material.
