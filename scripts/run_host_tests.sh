#!/bin/bash
#
# NeolyxOS - build and run every host unit test in tests/host/.
#
# Each test is built with the command written in its own header comment
# and passes when it exits 0 (and prints no "RESULT: FAIL").
# A new test file without an entry below fails this script, so every test
# that lands is run by CI.
#
# Usage: scripts/run_host_tests.sh [--werror] [--out DIR]
#   --werror   also build with the GCC 15 error flags
#              (implicit-function-declaration, implicit-int,
#              int-conversion, incompatible-pointer-types)
#   --out DIR  where binaries and logs go (default: build/host-tests)
#
# Exit status: 0 when every test built and passed, 1 otherwise.
#
# Copyright (c) 2025 KetiveeAI

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/host-tests"
EXTRA=()

while [ $# -gt 0 ]; do
    case "$1" in
        --werror) EXTRA=(-Werror=implicit-function-declaration -Werror=implicit-int
                         -Werror=int-conversion -Werror=incompatible-pointer-types); shift ;;
        --out)    [ $# -ge 2 ] || { echo "--out needs a folder" >&2; exit 2; }; OUT="$2"; shift 2 ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *)        echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

mkdir -p "$OUT" || { echo "cannot create $OUT" >&2; exit 1; }
cd "$ROOT" || exit 1

PASSED=0
FAILED=0
FAILED_NAMES=()

# run_test <name> <args after the binary> -- <gcc arguments>
# Builds $OUT/<name> from the gcc arguments (paths relative to the root),
# runs it, and checks the exit code and the RESULT line.
run_test() {
    local name="$1"; shift
    local run_args=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do run_args+=("$1"); shift; done
    [ "${1:-}" = "--" ] && shift
    local bin="$OUT/$name" log="$OUT/$name.log"

    if ! gcc "$@" "${EXTRA[@]}" -o "$bin" > "$OUT/$name.build.log" 2>&1; then
        echo "  FAIL  $name (build; see $OUT/$name.build.log)"
        sed 's/^/        /' "$OUT/$name.build.log" | head -20
        FAILED=$((FAILED + 1)); FAILED_NAMES+=("$name")
        return
    fi
    "$bin" "${run_args[@]}" > "$log" 2>&1
    local rc=$?
    if [ $rc -eq 0 ] && ! grep -q 'RESULT: FAIL' "$log"; then
        echo "  PASS  $name"
        PASSED=$((PASSED + 1))
    else
        echo "  FAIL  $name (exit $rc; see $log)"
        tail -15 "$log" | sed 's/^/        /'
        FAILED=$((FAILED + 1)); FAILED_NAMES+=("$name")
    fi
}

KNOWN=(bootlog_test mem_reclaim_test nx_fb_clip_test nx_malloc_test nxgfx_canvas_test
       nxs_damage_test nxw_check_test nxwindow_test pmm_test sched_queue_test)

# Every tests/host/*.c must be known here
for f in tests/host/*.c; do
    base="$(basename "$f" .c)"
    found=0
    for k in "${KNOWN[@]}"; do [ "$k" = "$base" ] && found=1; done
    if [ $found -eq 0 ]; then
        echo "  FAIL  $base (tests/host/$base.c has no entry in scripts/run_host_tests.sh)"
        FAILED=$((FAILED + 1)); FAILED_NAMES+=("$base")
    fi
done

echo "[HOST-TESTS] building and running tests/host (${EXTRA[*]:-default flags})"

run_test bootlog_test -- -std=gnu11 -Wall -Wextra -iquote kernel/include -iquote kernel/src/video \
    tests/host/bootlog_test.c kernel/src/video/bootlog.c
run_test mem_reclaim_test -- -std=c11 -O1 -Wall -Wextra -Ikernel/include tests/host/mem_reclaim_test.c
run_test nx_fb_clip_test -- -std=c11 -Wall -Wextra -iquote kernel/include tests/host/nx_fb_clip_test.c
run_test nx_malloc_test -- -std=c11 -O1 -Wall -Wextra -DNX_HOST_PREVIEW -Ikernel/include/core \
    tests/host/nx_malloc_test.c
run_test nxs_damage_test -- -std=c11 -O1 -Wall -Wextra tests/host/nxs_damage_test.c
run_test nxw_check_test -- -std=c11 -O1 -Wall -Wextra tests/host/nxw_check_test.c
run_test nxwindow_test -- -std=c11 -O1 -Wall -Wextra -DNX_HOST_PREVIEW -Isdk/nxwindow \
    -Idesktop/include -Ikernel/include/core tests/host/nxwindow_test.c
run_test pmm_test -- -std=c11 -O1 -Wall -Wextra -DPMM_HOST_TEST -Ikernel/include -no-pie \
    -Wl,--defsym=_kernel_end=0x200000 tests/host/pmm_test.c
run_test sched_queue_test -- -std=c11 -O1 -Wall -Wextra -Ikernel/include tests/host/sched_queue_test.c

# nxgfx_canvas_test needs the host build of the render library first
if make -C gui/nxrender_c host-test-lib > "$OUT/nxrender_host_lib.log" 2>&1; then
    run_test nxgfx_canvas_test resources/fonts/NXSans/NXSans-Text.ttf "$OUT/nxgfx.ppm" -- \
        -std=c11 -O1 -Wall -Wextra -Igui/nxrender_c/include tests/host/nxgfx_canvas_test.c \
        gui/nxrender_c/build/host/libnxrender_host.a -lm
else
    echo "  FAIL  nxgfx_canvas_test (host render library build; see $OUT/nxrender_host_lib.log)"
    FAILED=$((FAILED + 1)); FAILED_NAMES+=(nxgfx_canvas_test)
fi

echo "[HOST-TESTS] $PASSED passed, $FAILED failed${FAILED_NAMES[*]:+: ${FAILED_NAMES[*]}}"
[ $FAILED -eq 0 ]
