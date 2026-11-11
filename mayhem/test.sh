#!/usr/bin/env bash
#
# mayhem/test.sh: RUN the X server's own unit tests plus known-answer probes (both built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# Upstream's unit tests (test/tests, normal flags). Each suite forks per case and prints " Pass" or " FAIL";
# XLIBRE_TEST selects one suite. These four fail on upstream's own code at this commit, so they are skipped:
#   signal_logging_test, protocol_xigetselectedevents_test, protocol_xipassivegrabdevice_test,
#   protocol_eventconvert_test
TESTS="$SRC/mayhem/build-tests/test/tests"
PROBE="$SRC/kat_probe"
[ -x "$TESTS" ] || { echo "missing $TESTS: mayhem/build.sh did not build it" >&2; F="$SRC/mayhem/io_framing/testsuite"
kat "type=43 len=1
type=16 len=4
type=1 len=8
type=43 len=1
requests=4" frame "$F/little.bin"
kat "type=43 len=1
type=16 len=4
type=1 len=8
requests=3" frame "$F/swapped.bin"
kat "type=16 len=4
type=43 len=1
requests=2" frame "$F/bytewise.bin"
kat "type=43 len=1
type=127 len=5
type=43 len=1
requests=3" frame "$F/bigreq.bin"
kat "type=127 len=5
type=43 len=1
requests=2" frame "$F/bigreq_swapped.bin"

emit_ctrf "xserver" 0 1; exit 1; }
[ -x "$PROBE" ] || { echo "missing $PROBE: mayhem/build.sh did not build it" >&2; F="$SRC/mayhem/io_framing/testsuite"
kat "type=43 len=1
type=16 len=4
type=1 len=8
type=43 len=1
requests=4" frame "$F/little.bin"
kat "type=43 len=1
type=16 len=4
type=1 len=8
requests=3" frame "$F/swapped.bin"
kat "type=16 len=4
type=43 len=1
requests=2" frame "$F/bytewise.bin"
kat "type=43 len=1
type=127 len=5
type=43 len=1
requests=3" frame "$F/bigreq.bin"
kat "type=127 len=5
type=43 len=1
requests=2" frame "$F/bigreq_swapped.bin"

emit_ctrf "xserver" 0 1; exit 1; }

SUITES="list_test list_zeroinit_test sha1_test string_test fixes_test input_test misc_test touch_test
xfree86_test xkb_test xtest_test ddc_tests hashtabletest_test protocol_xchangedevicecontrol_test
protocol_xiqueryversion_test protocol_xiquerydevice_test protocol_xiselectevents_test
protocol_xisetclientpointer_test protocol_xigetclientpointer_test protocol_xiquerypointer_test
protocol_xiwarppointer_test xi2_test"
SKIPPED=4

passed=0 failed=0
for t in $SUITES; do
  out=$(XLIBRE_TEST=$t timeout 300 "$TESTS" 2>&1)
  rc=$?
  # Pass needs the suite's own " Pass" verdict, not just an exit status.
  if [ "$rc" -eq 0 ] && printf '%s\n' "$out" | grep -qx ' Pass'; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1)); echo "FAIL unit suite $t (rc=$rc)" >&2; printf '%s\n' "$out" | tail -5 >&2
  fi
done

# Known-answer probes: decode a committed input with the server's own parser and compare what it found.
kat() {
  local want="$1"; shift
  local got
  got=$("$PROBE" "$@" 2>&1)
  if [ "$got" = "$want" ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1)); echo "FAIL probe $*" >&2; echo "  want: $want" >&2; echo "  got:  $got" >&2
  fi
}
E="$SRC/mayhem/edid/testsuite"
kat "vendor=HPN prod=13847 version=1.3 size=60x34 sections=1
hdr=1 pq=1 hlg=0 min=0.000093 max=603.665771 maxfall=351.250427" edid "$E/hp_5dq99aa_hdmi_edid.bin"
kat "vendor=SAM prod=28734 version=1.3 size=165x93 sections=1
hdr=1 pq=1 hlg=1 min=0.000000 max=0.000000 maxfall=0.000000" edid "$E/samsung_q800t_hdmi2_0_edid.bin"
kat "vendor=MSI prod=15778 version=1.4 size=70x39 sections=1
hdr=1 pq=1 hlg=0 min=0.000000 max=366.801605 maxfall=366.801605" edid "$E/msi_mag321curv_dp_edid.bin"
kat "vendor=RHT prod=4660 version=1.4 size=32x20 sections=1
hdr=0 pq=0 hlg=0 min=0.000000 max=0.000000 maxfall=0.000000" edid "$E/qemu_edid.bin"
K="$SRC/mayhem/xkm_read/testsuite"
kat "unread=0x0 min_key=8 max_key=255 types=24
key38=AC01 key65=SPCE" xkm "$K/keymap1.xkm"
kat "unread=0x20 min_key=8 max_key=255 types=24
key38=AC01 key65=SPCE" xkm "$K/keymap5.xkm"
C="$SRC/mayhem/xorg_conf/testsuite"
kat "device=Card0 driver=intel busid=PCI:0:2:0
monitor=Monitor0" conf "$C/monitor.conf"
kat "device=Card0 driver=vesa busid=-" conf "$C/minimal.conf"

F="$SRC/mayhem/io_framing/testsuite"
kat "type=43 len=1
type=16 len=4
type=1 len=8
type=43 len=1
requests=4" frame "$F/little.bin"
kat "type=43 len=1
type=16 len=4
type=1 len=8
requests=3" frame "$F/swapped.bin"
kat "type=16 len=4
type=43 len=1
requests=2" frame "$F/bytewise.bin"
kat "type=43 len=1
type=127 len=5
type=43 len=1
requests=3" frame "$F/bigreq.bin"
kat "type=127 len=5
type=43 len=1
requests=2" frame "$F/bigreq_swapped.bin"

emit_ctrf "xserver" "$passed" "$failed" "$SKIPPED"
