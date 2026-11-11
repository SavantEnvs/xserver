#!/usr/bin/env bash
#
# mayhem/build.sh: build the X.Org-derived X server's fuzz harnesses and its unit-test oracle.
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem, offline.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# Upstream's own options, minus the DDXes: the harnesses link the server's static libraries the way upstream's
# unit tests do and need no DDX binary.
MESON_COMMON=(-Dxorg=true -Dxvfb=false -Dxnest=false -Dxephyr=false -Dglamor=false -Ddocs=false -Ddevel-docs=false)

# 1) Sanitized server (every static server library carries ASan/UBSan, SanCov and DWARF 3).
#    -fno-sanitize=shift-base: the EDID serial number is assembled as `byte << 24` in an int, which UBSan
#    halts on for any serial with the high bit set, so no EDID could get past the base block otherwise.
FUZZ_BUILD="$SRC/mayhem/build-fuzz"
rm -rf "$FUZZ_BUILD"
meson setup "$FUZZ_BUILD" "${MESON_COMMON[@]}" \
    -Db_lundef=false \
    -Dc_args="$SANITIZER_FLAGS -fno-sanitize=shift-base -fsanitize=fuzzer-no-link $DEBUG_FLAGS" \
    -Dc_link_args="$SANITIZER_FLAGS"
# Building test/tests builds every static server library and links them the way a unit test does:
# that link line is the one reused for the harnesses.
ninja -C "$FUZZ_BUILD" -j"$MAYHEM_JOBS" test/tests

CFLAGS_H=$(python3 mayhem/mesonflags.py compile "$FUZZ_BUILD")
LDFLAGS_H=$(python3 mayhem/mesonflags.py link "$FUZZ_BUILD")

# 2) Harnesses: fuzzer binary + standalone reproducer each, with LSan switched off at build time.
cd "$FUZZ_BUILD"
mkdir -p harness
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o harness/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o harness/standalone_main.o
for h in xkm_read xorg_conf edid io_framing; do
    # shellcheck disable=SC2086
    $CC $CFLAGS_H -c "$SRC/mayhem/harnesses/$h.c" -o "harness/$h.o"
    # shellcheck disable=SC2086
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "harness/$h.o" test/tests.p/.._mi_miinitext.c.o harness/lsan_off.o $LDFLAGS_H -o "$SRC/$h"
    # shellcheck disable=SC2086
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS "harness/$h.o" harness/standalone_main.o test/tests.p/.._mi_miinitext.c.o harness/lsan_off.o $LDFLAGS_H -o "$SRC/$h-standalone"
done
cd "$SRC"

# 3) Upstream's unit tests, normal flags, un-sanitized: test.sh only runs them.
TEST_BUILD="$SRC/mayhem/build-tests"
rm -rf "$TEST_BUILD"
meson setup "$TEST_BUILD" "${MESON_COMMON[@]}" \
    -Dc_args="$COVERAGE_FLAGS" -Dc_link_args="$COVERAGE_FLAGS"
ninja -C "$TEST_BUILD" -j"$MAYHEM_JOBS" test/tests

# Known-answer probe (see mayhem/test.sh): upstream's parsers linked into a plain, dynamically linked tool.
cd "$TEST_BUILD"
# shellcheck disable=SC2046
$CC -O2 -c $(python3 "$SRC/mayhem/mesonflags.py" compile "$TEST_BUILD") "$SRC/mayhem/probes/kat_probe.c" -o kat_probe.o
# shellcheck disable=SC2046
$CC -O2 $COVERAGE_FLAGS kat_probe.o test/tests.p/.._mi_miinitext.c.o $(python3 "$SRC/mayhem/mesonflags.py" link "$TEST_BUILD") -o "$SRC/kat_probe"
