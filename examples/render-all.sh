#!/usr/bin/env bash
# Regenerate every docs/images/* file from the self-contained examples.
# Each example computes with lmbio (the build/ binary when present), draws with lmplot or lmdraw
# (installed by lmscript from their GitHub releases) and writes images/<name>.png; animated
# examples also write images/<name>.gif when ffmpeg is on PATH and say SKIP otherwise.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$PWD/build:$PATH"
compiler="${LMSCRIPT_BIN:-$(scripts/select-compiler.sh)}"
mkdir -p docs/images
for program in examples/*.lms; do
  name="$(basename "$program" .lms)"
  "$compiler" "$program"
  cp "images/$name.png" "docs/images/$name.png"
  if [ -f "images/$name.gif" ]; then cp "images/$name.gif" "docs/images/$name.gif"; fi
done
command -v ffmpeg >/dev/null 2>&1 || echo "SKIP animations: ffmpeg unavailable, committed GIFs kept (all PNGs regenerated)"
python3 scripts/check-gallery.py --generated
