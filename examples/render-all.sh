#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$PWD/build:$PATH"
compiler="${LMSCRIPT_BIN:-$(scripts/select-compiler.sh)}"
mkdir -p build/examples build/visual docs/images
for program in examples/*.lms; do "$compiler" "$program"; done
python3 scripts/check-gallery.py
