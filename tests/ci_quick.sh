#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# 5-LAB: the host tests that need only a C compiler and build/gen (run ./build.sh once first, or the generator it calls).
# A short list for CI (.github/workflows/build.yml); tests/run_tests.sh is the full suite. Exit 1 if any fails.
#   sh tests/ci_quick.sh
cd "$(dirname "$0")/.." || exit 1
[ -f build/gen/felucca_tables.h ] || { echo "ci_quick: no build/gen (run ./build.sh first)"; exit 1; }
O=build/host; mkdir -p "$O"
CC="${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests"
fail=0
t() {   # name [extra compiler flags]
    n=$1; shift
    if ! $CC "$@" -o "$O/$n" "tests/$n.c" -lm 2>"$O/$n.cc"; then echo "FAIL  $n (does not compile)"; head -3 "$O/$n.cc"; fail=1; return; fi
    if timeout 900 "$O/$n" >"$O/$n.out" 2>&1; then echo "ok    $n: $(tail -1 "$O/$n.out" | cut -c1-90)"
    else echo "FAIL  $n"; grep FAIL "$O/$n.out" | head -3; tail -n 12 "$O/$n.out" | cut -c1-200; fail=1; fi
}
for n in click_test editor_test midi_control_test motion_test ramp_test nudge_roll_test notelen_test pernote_test ratchet_test step15_test detail_test \
         seq155_test ui155_test v156_test v16_test swing_test project_test proj5_test persistence_test settings_test backup_test \
         robust_test midi_uart_test ui_test; do
    t "$n"
done
if $CC -DFELUCCA_CPU2=0 -o "$O/editor_test_c1" tests/editor_test.c -lm 2>/dev/null && "$O/editor_test_c1" >/dev/null 2>&1; then
    echo "ok    editor_test (FELUCCA_CPU2=0)"; else echo "FAIL  editor_test (FELUCCA_CPU2=0)"; fail=1; fi
if $CC -DFELUCCA_CPU2=0 -o "$O/ui_test_c1" tests/ui_test.c -lm 2>/dev/null && "$O/ui_test_c1" >/dev/null 2>&1; then
    echo "ok    ui_test (FELUCCA_CPU2=0)"; else echo "FAIL  ui_test (FELUCCA_CPU2=0)"; fail=1; fi
# the sound must not change: every render against tests/golden.txt, and the CPU cost
if $CC -O2 -o "$O/regress" tests/regress.c -lm 2>/dev/null && timeout 900 "$O/regress" tests/golden.txt tests/cpu_baseline.txt >"$O/regress.out" 2>&1 \
   && tail -1 "$O/regress.out" | grep -q "(0 changed, 0 gone), 0 health failures, 0 voice / routing checks failed, 0 CPU entries over budget"; then
    echo "ok    regress: $(tail -1 "$O/regress.out" | cut -c1-100)"; else echo "FAIL  regress"; tail -3 "$O/regress.out"; fail=1; fi
[ "$fail" = 0 ] && echo "ci_quick: all passed" || echo "ci_quick: FAILED"
exit $fail
