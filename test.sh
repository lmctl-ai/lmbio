#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
BUILD_DIR="${BUILD_DIR:-build}"
HOST="$BUILD_DIR/lmbio"
UNAME_S="$(uname -s)"
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) HOST="$HOST.exe";; esac
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lmbio-test-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
# Native Windows tools (node, the host) cannot address git-bash /tmp or /d/a paths.
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) WORK="$(cd "$WORK" && pwd -W | tr '\134' '/')";; esac
pass=0
ok(){ pass=$((pass+1)); echo "ok $pass - $1"; }
fail(){ echo "FAIL: $*" >&2; exit 1; }
sha(){ if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi; }

# lmscript resolves cli="lmbio" on PATH first, so programs bind to the binary under test.
PATH="$(cd "$BUILD_DIR" 2>/dev/null && pwd || echo "$HERE/$BUILD_DIR"):$PATH"
export PATH
LMSCRIPT="$(scripts/select-compiler.sh)"
scripts/sync-sdk.sh --verify >/dev/null || fail "SDK header pin mismatch"
ok "SDK header matches sdk.lock"
scripts/sync-deps.sh --verify >/dev/null || fail "third_party pin mismatch"
ok "third_party matches deps.lock"

GEN=""
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then GEN="-G Ninja"; fi
# shellcheck disable=SC2086
cmake -S . -B "$BUILD_DIR" $GEN -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" --parallel "${BUILD_JOBS:-4}" >"$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log" >&2; fail "build"; }
ok "build"
ctest --test-dir "$BUILD_DIR" --output-on-failure || fail "native biochemical tests"
ok "native biochemical regression tests"
want_version="lmbio $(sed -n 's/^project(lmbio VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ "$("$HOST" --version)" = "$want_version" ] || fail "--version is not exactly '$want_version'"
ok "--version prints exactly '$want_version'"
cp schema/lmbio.dsl.json "$WORK/schema.committed"
python3 scripts/gen-schema.py >/dev/null || fail "scripts/gen-schema.py"
cmp -s "$WORK/schema.committed" schema/lmbio.dsl.json || fail "schema/lmbio.dsl.json is stale: run scripts/gen-schema.py"
ok "schema matches the registrations in cpp/main.cpp"
"$HOST" --dsl-schema --json >"$WORK/schema.json" || fail "embedded schema print"
cmp -s "$WORK/schema.json" schema/lmbio.dsl.json || fail "embedded schema differs from schema/lmbio.dsl.json"
ok "--dsl-schema --json prints byte-identical embedded schema bytes"
diff -u <("$LMSCRIPT" --dsl-signatures schema/lmbio.dsl.json) <("$HOST" --dsl-signatures) || fail "host/compiler signatures differ"
ok "host registry matches compiler signatures"
"$HOST" --notices >"$WORK/notices.txt" || fail "--notices"
cmp -s "$WORK/notices.txt" THIRD_PARTY_NOTICES || fail "--notices differs from THIRD_PARTY_NOTICES"
for name in SUNDIALS pugixml cpp-dsl-sdk glibc MinGW; do grep -q "$name" "$WORK/notices.txt" || fail "--notices lacks $name"; done
ok "--notices prints the third-party license texts"

case "$UNAME_S" in
  Linux)
    if command -v readelf >/dev/null 2>&1; then
      needed="$(readelf -d "$HOST" | sed -n 's/.*(NEEDED).*Shared library: \[\(.*\)\]/\1/p')"
    else
      needed="$(ldd "$HOST" | awk '/=>/ {print $1} !/=>/ && /ld-linux/ {sub(/^[[:space:]]*/, ""); sub(/[[:space:]].*/, ""); print $0}' | xargs -n1 basename)"
    fi
    [ -n "$needed" ] || fail "expected dynamic glibc linkage on Linux, found no NEEDED entries"
    bad="$(printf '%s\n' "$needed" | grep -v -E '^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|ld-linux.*)' || true)"
    [ -z "$bad" ] || fail "disallowed Linux dynamic libraries: $bad"
    ok "Linux binary links only dynamic glibc (${needed//$'\n'/ })";;
  Darwin) bad="$(otool -L "$HOST" | tail -n +2 | grep -v -E '/usr/lib/(libSystem|libc\+\+|libresolv)' || true)"
          [ -z "$bad" ] || fail "non-system dylib dependencies: $bad"; ok "only system libraries linked";;
  MINGW*|MSYS*|CYGWIN*)
    if command -v objdump >/dev/null 2>&1; then
      bad="$(objdump -p "$HOST" | grep 'DLL Name' | grep -i -E 'libstdc|libgcc|winpthread|libgomp' || true)"
      [ -z "$bad" ] || fail "MinGW runtime DLLs are imported: $bad"
      ok "no MinGW runtime DLL imports"
    else ok "static-link check skipped (no objdump)"; fi;;
  *) ok "static-link check skipped on $UNAME_S";;
esac

# Programs: compile every test and example, run them (they assert their own numbers).
mkdir -p build/examples images
rm -f images/*.png
n=0
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$LMSCRIPT" "$src" "build/$name.dslb" >"$WORK/$name.log" 2>&1 || { cat "$WORK/$name.log" >&2; fail "$name compile"; }
  n=$((n+1))
done
ok "$n programs compile as bytecode v5"
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --dsl "build/$name.dslb" >"$WORK/$name.out" 2>"$WORK/$name.err" || { cat "$WORK/$name.out" "$WORK/$name.err" >&2; fail "$name run"; }
  grep -q '"ok": true' "$WORK/$name.out" || { cat "$WORK/$name.out" >&2; fail "$name did not report ok"; }
done
ok "$n programs run and pass their own assertions"

# Determinism: the CSV files the examples write are identical on rerun.
for f in build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/before.txt"
for src in examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --dsl "build/$name.dslb" >/dev/null 2>&1 || fail "$name rerun"
done
for f in build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/after.txt"
cmp -s "$WORK/before.txt" "$WORK/after.txt" || fail "result files differ on rerun"
ok "all $(wc -l <"$WORK/after.txt" | tr -d ' ') result files are byte-identical on rerun"

# Error paths: each snippet must fail the run with exit 3 and a message naming the problem.
expect_error() { # name, snippet, expected message fragment
  cat >"$WORK/$1.lms" <<LMS
p = _dsl_ cli="lmbio";
$2
end _dsl_
savedsl(p, "$WORK/$1.dslb");
LMS
  "$LMSCRIPT" "$WORK/$1.lms" >/dev/null 2>"$WORK/$1.cerr" || { cat "$WORK/$1.cerr" >&2; fail "$1 compile"; }
  set +e; out="$("$HOST" --dsl "$WORK/$1.dslb" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] || fail "$1: exit code $code, expected 3 ($out)"
  echo "$out" | grep -q "$3" || fail "$1: message lacks '$3': $out"
}
expect_error badtol 'let m = bio.model("bad").tol(0.0, 0.000001);' "tolerance"
expect_error badvolume 'let m = bio.model("bad").compartment("c", 0.0);' "compartment"
expect_error duplicate 'let m = bio.model("bad").compartment("c", 1.0).species("A", "c", 1.0).species("A", "c", 2.0);' "duplicate"
expect_error compartment 'let m = bio.model("bad").species("A", "missing", 1.0);' "compartment"
expect_error unknown 'let m = bio.model("bad").compartment("c",1.0).species("A","c",1.0).reaction("r","A -> B","A"); let r = m.simulate(0.0,1.0,2);' "species"
expect_error expression 'let m = bio.model("bad").compartment("c",1.0).species("A","c",1.0).reaction("r","A ->","2 +* A"); let r = m.simulate(0.0,1.0,2);' "expression"
expect_error grid 'let m = bio.model("bad").compartment("c",1.0).species("A","c",1.0); let r = m.simulate(1.0,0.0,2);' "simulate"
expect_error cycle 'let m = bio.model("bad").compartment("c",1.0).species("A","c",1.0).rule("A","A+1"); let r = m.simulate(0.0,1.0,2);' "cycle"
expect_error negative_propensity 'let m = bio.model("bad").compartment("c",1.0).species("A","c",1.0).reaction("r","A ->","-1"); let r = m.ssa(1.0,2,1);' "propensity"
expect_error missing_xml 'let m = bio.load_sbml("missing-lmbio.xml");' "SBML"
expect_error result_index 'let m = bio.model("bad").compartment("c",1.0).species("A","c",1.0); let r = m.simulate(0.0,1.0,2); let x = r.value("A",2);' "row"
expect_error result_column 'let m = bio.model("bad").compartment("c",1.0).species("A","c",1.0); let r = m.simulate(0.0,1.0,2); let x = r.final("Z");' "unknown result column: Z"
expect_error assertfail 'bio.assert_close(1.0,2.0,0.1);' "assert_close"
expect_error nan_assert 'bio.assert_close(sqrt(-1.0),0.0,0.1);' "assert_close"
expect_error assert_true 'bio.assert_true(false);' "assert_true"
expect_error never_crosses 'let m = bio.model("bad").compartment("c",1.0).species("A","c",1.0); let x = m.simulate(0.0,1.0,3).crossing("A",2.0);' "never reaches"
expect_error append_columns 'let a = bio.model("a").compartment("c",1.0).species("A","c",1.0).simulate(0.0,1.0,2); let b = bio.model("b").compartment("c",1.0).species("B","c",1.0).simulate(1.0,2.0,2); let x = a.append(b);' "same columns"
expect_error mass_action_species 'let m = bio.model("bad").compartment("c",1.0).species("A","c",1.0).mass_action("r","A + Z -> A","1.0");' "unknown species in reaction: Z"
expect_error compartment_size 'let m = bio.model("bad").compartment("c",1.0).set("c",0.0);' "compartment size"
ok "errors stop with exit 3 and clear messages (19 cases)"

# Dry run loads/verifies bytecode without executing numerical work or writing CSV.
rm -f build/examples/biochemical_network.csv
"$HOST" --dry-run --dsl build/biochemical_network.dslb >"$WORK/dry.out" || fail "dry run"
[ ! -e build/examples/biochemical_network.csv ] || fail "dry run created CSV"
"$HOST" --dsl build/biochemical_network.dslb >/dev/null || fail "restore example CSV"
ok "dry run verifies without writing outputs"

for flags in '--unknown' '--version extra' '--dsl-schema --bogus' '--dry-run' '--dsl --bogus'; do
  set +e
  # Deliberate word splitting: each fixed test string is a CLI argument sequence.
  # shellcheck disable=SC2086
  "$HOST" $flags >/dev/null 2>&1
  code=$?
  set -e
  [ "$code" = 2 ] || fail "usage flags '$flags': exit $code"
done
ok "invalid CLI combinations use exit 2"

set +e; "$HOST" --dsl "$WORK/does-not-exist.dslb" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 3 ] || fail "missing bytecode exit code $code, expected 3"
set +e; "$HOST" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 2 ] || fail "usage exit code $code, expected 2"
ok "usage and load errors use exit codes 2 and 3"

# Memory budget: a runaway loop stops with a run error (exit 3, message on stderr and in the JSON
# line) instead of exhausting the machine. A small budget everywhere; on Linux also the default
# 4096 MiB under a 6 GiB address-space limit (ulimit only here, never in the host). Each Result is
# 1001 columns x 9990 rows (80 MB); t1 == t0 skips the solver, so the loop only allocates.
cat >"$WORK/runaway.lms" <<LMS
p = _dsl_ cli="lmbio";
let m = bio.model("runaway").compartment("c", 1.0).species("a", "c", 1.0);
for (j in range(0, 999)) {
  m = m.param("k" + str(j), 1.0);
}
let i = 0;
while (i < 1000000) {
  let r = m.simulate(0.0, 0.0, 9990);
  i = i + 1;
}
end _dsl_
savedsl(p, "$WORK/runaway.dslb");
LMS
"$LMSCRIPT" "$WORK/runaway.lms" >/dev/null || fail "runaway.lms compile"
set +e; out="$(LMBIO_MAX_MEMORY_MB=64 "$HOST" --dsl "$WORK/runaway.dslb" 2>"$WORK/runaway.err")"; code=$?; set -e
[ "$code" = 3 ] || fail "runaway loop under LMBIO_MAX_MEMORY_MB=64: exit $code, expected 3 ($out)"
echo "$out" | grep -q "memory budget exceeded" || fail "runaway loop: JSON lacks the budget message: $out"
grep -q "LMBIO_MAX_MEMORY_MB" "$WORK/runaway.err" || fail "runaway loop: no budget message on stderr"
set +e; LMBIO_MAX_MEMORY_MB=-5 "$HOST" --dsl "$WORK/runaway.dslb" >/dev/null 2>"$WORK/badenv.err"; code=$?; set -e
[ "$code" = 2 ] && grep -q "positive whole number" "$WORK/badenv.err" || fail "bad LMBIO_MAX_MEMORY_MB: exit $code"
# Memory taken outside operator new is counted too: SUNDIALS state is charged before CVODE is
# created (2000 species: a 31 MiB dense Jacobian plus its saved copy) and pugixml allocates
# through the counted operator new (a 24 MiB SBML file is read whole).
cat >"$WORK/wide.lms" <<LMS
p = _dsl_ cli="lmbio";
let m = bio.model("wide").compartment("c", 1.0);
for (j in range(0, 2000)) {
  m = m.species("s" + str(j), "c", 1.0);
}
let r = m.simulate(0.0, 1.0, 2);
end _dsl_
savedsl(p, "$WORK/wide.dslb");
LMS
"$LMSCRIPT" "$WORK/wide.lms" >/dev/null || fail "wide.lms compile"
set +e; out="$(LMBIO_MAX_MEMORY_MB=32 "$HOST" --dsl "$WORK/wide.dslb" 2>/dev/null)"; code=$?; set -e
[ "$code" = 3 ] && echo "$out" | grep -q "memory budget exceeded" || fail "CVODE state over LMBIO_MAX_MEMORY_MB=32: exit $code ($out)"
python3 -c 'import sys; open(sys.argv[1], "w").write("<?xml version=\"1.0\"?>\n<sbml><!-- " + "x" * (24 << 20) + " --></sbml>\n")' "$WORK/big.xml"
cat >"$WORK/bigxml.lms" <<LMS
p = _dsl_ cli="lmbio";
let m = bio.load_sbml("$WORK/big.xml");
end _dsl_
savedsl(p, "$WORK/bigxml.dslb");
LMS
"$LMSCRIPT" "$WORK/bigxml.lms" >/dev/null || fail "bigxml.lms compile"
set +e; out="$(LMBIO_MAX_MEMORY_MB=16 "$HOST" --dsl "$WORK/bigxml.dslb" 2>/dev/null)"; code=$?; set -e
[ "$code" = 3 ] && echo "$out" | grep -q "memory budget exceeded" || fail "24 MiB SBML over LMBIO_MAX_MEMORY_MB=16: exit $code ($out)"
rm -f "$WORK/big.xml"
if [ "$UNAME_S" = Linux ]; then
  set +e; out="$(ulimit -v 6291456; "$HOST" --dsl "$WORK/runaway.dslb" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] && echo "$out" | grep -q "limit 4096 MiB" || fail "runaway loop with the default budget under ulimit -v 6 GiB: exit $code ($out)"
  ok "memory budget: runaway loop stops at 64 MiB and at the default 4096 MiB (under ulimit -v 6 GiB); CVODE state and SBML documents count"
else
  ok "memory budget: runaway loop stops at LMBIO_MAX_MEMORY_MB=64 (default-budget run is Linux-only); CVODE state and SBML documents count"
fi

python3 scripts/check-gallery.py --generated
ok "gallery links, image signatures, dimensions and byte budgets"

echo "all $pass checks passed"
