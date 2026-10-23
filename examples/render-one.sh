#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v lmplot >/dev/null 2>&1 && [ "${VISUALS_NETWORK:-0}" != 1 ]; then
  echo "SKIP visual $1: lmplot unavailable; add it to PATH or set VISUALS_NETWORK=1 for compiler-managed installation"
  exit 0
fi
compiler="${LMSCRIPT_BIN:-$(scripts/select-compiler.sh)}"
"$compiler" "examples/visuals/$1.lms"
if [ "$1" = "decay_clock" ]; then
  if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -y -v error -threads 1 -framerate 8 -i "build/visual/decay_clock-frame-%d.png" -vf scale=600:-1 -loop 0 "docs/images/decay_clock.gif"
  else echo "SKIP animation: ffmpeg unavailable (PNG still regenerated)"; fi
fi
