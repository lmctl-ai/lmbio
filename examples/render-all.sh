#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$PWD/build:$PATH"
compiler="${LMSCRIPT_BIN:-$(scripts/select-compiler.sh)}"
mkdir -p docs/images
for program in examples/*.lms; do
  "$compiler" "$program"
  name="$(basename "$program" .lms)"
  cp "images/$name.png" "docs/images/$name.png"
done
if command -v ffmpeg >/dev/null 2>&1; then
  ffmpeg -y -v error -threads 1 -framerate 8 -i "build/visual/decay_clock-frame-%d.png" -vf scale=600:-1 -loop 0 "images/decay_clock.gif"
  cp "images/decay_clock.gif" "docs/images/decay_clock.gif"
else echo "SKIP animation: ffmpeg unavailable (all PNGs regenerated)"; fi
python3 scripts/check-gallery.py --generated
