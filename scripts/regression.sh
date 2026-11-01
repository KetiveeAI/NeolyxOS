#!/bin/bash
#
# NeolyxOS - full regression on the current tree, with VM performance
# numbers. S10 runs it on main twice a week and posts the report.
#
# Steps:
#   1. syscall registry check, host tests (GCC 15 error flags)
#   2. ./build_all.sh --werror and ./rebuild_image.sh
#   3. for each VM profile, one QEMU boot that:
#        - measures boot time to the desktop and idle host CPU,
#        - takes a memory snapshot (memprobe),
#        - runs every in-QEMU test from the app drawer,
#        - takes a memory snapshot after the apps closed,
#        - runs cpuhog (frame time under CPU load),
#        - runs memstress (survival under memory pressure; low profile
#          only: its 40 x 64 MiB children cannot fill a 4 GiB VM),
#        - takes a last memory snapshot.
#   4. writes report.md and keeps every log.
#
# VM profiles (owner decision 2026-11-01: VMs first, low resources matter):
#   low     1 vCPU, 1 GiB
#   normal  2 vCPU, 4 GiB
#
# Usage: scripts/regression.sh [--out DIR] [--profiles "low normal"]
#                              [--skip-build] [--skip "test ..."]
#   --skip   leave tests out, for example "nettest" on a host without
#            outside DNS; skipped tests are listed in the report
# Exit status: 0 when everything passed, 1 otherwise.
#
# Copyright (c) 2025 KetiveeAI

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAMP="$(date -u +%Y-%m-%d-%H%M)"
OUT="$ROOT/build/regression/$STAMP"
PROFILES="low normal"
SKIP_BUILD=0
SKIP_TESTS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --out)        [ $# -ge 2 ] || { echo "--out needs a folder" >&2; exit 2; }; OUT="$2"; shift 2 ;;
        --profiles)   [ $# -ge 2 ] || { echo "--profiles needs names" >&2; exit 2; }; PROFILES="$2"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --skip)       [ $# -ge 2 ] || { echo "--skip needs test names" >&2; exit 2; }; SKIP_TESTS="$2"; shift 2 ;;
        -h|--help)    sed -n '2,25p' "$0"; exit 0 ;;
        *)            echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
for p in $PROFILES; do
    case "$p" in low|normal) ;; *) echo "unknown profile '$p' (low, normal)" >&2; exit 2 ;; esac
done

mkdir -p "$OUT" || { echo "cannot create $OUT" >&2; exit 1; }
cd "$ROOT" || exit 1
REPORT="$OUT/report.md"
STATUS=0
log() { echo "[REGRESSION] $*"; }

# Tests run in each boot, in order. NAME:TIMEOUT[:TEXT] as for qemu_run.sh.
# nxuitest's last part needs a person, so it passes once its automatic
# checks are done and its window is open.
TESTS=(
    memprobe:60
    fdtest:120 shmtest:120 ipc_test:120 fbtest:120 classtest:300
    nettest:240
    wintest:300 nxwtest:300
    "nxuitest:300:window open: click the buttons"
    memprobe:60
    "cpuhog:120:kloops in 20 s"
    "memstress:600:[MEMSTRESS] done:"
    memprobe:60
)

COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
BRANCH="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
{
    echo "# NeolyxOS regression $STAMP"
    echo
    echo "Commit \`$COMMIT\` on \`$BRANCH\`. Host: $(nproc) CPUs, GCC $(gcc -dumpfullversion 2>/dev/null || echo '?')."
    [ -n "$SKIP_TESTS" ] && echo && echo "Skipped on purpose: $SKIP_TESTS."
    echo
} > "$REPORT"

# ---- 1. Registry and host tests ----
if scripts/check_syscall_registry.sh > "$OUT/registry.log" 2>&1; then reg=PASS; else reg=FAIL; STATUS=1; fi
if scripts/run_host_tests.sh --werror --out "$OUT/host-tests" > "$OUT/host-tests.log" 2>&1; then host=PASS; else host=FAIL; STATUS=1; fi
log "registry $reg, host tests $host"

# ---- 2. Build ----
build=skipped
if [ "$SKIP_BUILD" -eq 0 ]; then
    if ./build_all.sh --werror > "$OUT/build.log" 2>&1 && ./rebuild_image.sh >> "$OUT/build.log" 2>&1; then
        build=PASS
    else
        build=FAIL; STATUS=1
    fi
    # The build rewrites committed artifacts; leave the tree as it was.
    git checkout -- desktop/bin/neolyx-desktop 2>/dev/null || true
fi
log "build $build"

{
    echo "| Check | Result |"
    echo "|---|---|"
    echo "| Syscall registry | $reg |"
    echo "| Host tests (GCC 15 flags) | $host ($(tail -1 "$OUT/host-tests.log" | sed 's/^\[HOST-TESTS\] //')) |"
    echo "| Build with GCC 15 flags | $build |"
    echo
} >> "$REPORT"

# ---- 3. VM profiles ----
# frames <serial> <first-last>: "avg_draw_ms max_draw_ms frames_drawn seconds"
# from the desktop's "[DESKTOP] Frames:" lines in that line range.
frames() {
    local serial="$1" range="$2"
    [ -n "$range" ] || { echo "- - - -"; return; }
    sed -n "${range%-*},${range#*-}p" "$serial" | tr -d '\r' | awk '
        /^\[DESKTOP\] Frames:/ {
            for (i = 1; i <= NF; i++) {
                if ($i == "in" && $(i+2) == "ms:") ms += $(i+1)
                if ($i == "draw" && $(i+2) == "ms,") { d = $(i+1); sum += d; n++; if (d > max) max = d }
                if ($i == "full," ) drawn += $(i-1)
                if ($i == "partial") drawn += $(i-1)
            }
        }
        END {
            if (n == 0) { print "- - - -"; exit }
            printf "%.0f %d %d %.0f\n", sum / n, max, drawn, ms / 1000
        }'
}

summary_get() { grep -m1 "^$2=" "$1" 2>/dev/null | cut -d= -f2-; }

# memprobe line n (1-based) from a serial log: "free_kb user_kb level"
memprobe() {
    grep -a '^\[MEMPROBE\] free_kb=' "$1" | tr -d '\r' | sed -n "${2}p" | awk '{
        for (i = 2; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
        printf "%s %s %s %s\n", v["free_kb"], v["user_kb"], v["shm_kb"], v["level"]
    }'
}

for p in $PROFILES; do
    case "$p" in
        low)    cpus=1; mem=1G ;;
        normal) cpus=2; mem=4G ;;
    esac
    dir="$OUT/vm-$p"
    args=(--cpus "$cpus" --mem "$mem" --boot-timeout 600 --idle 30 --screenshot --out "$dir")
    for t in "${TESTS[@]}"; do
        skip=0
        [ "$p" != low ] && [ "${t%%:*}" = memstress ] && skip=1
        for x in $SKIP_TESTS; do [ "${t%%:*}" = "$x" ] && skip=1; done
        [ $skip -eq 0 ] && args+=(--run "$t")
    done
    log "VM profile $p: $cpus vCPU, $mem"
    if scripts/qemu_run.sh "${args[@]}" > "$OUT/vm-$p.log" 2>&1; then vm=PASS; else vm=FAIL; STATUS=1; fi
    s="$dir/summary.txt"
    serial="$dir/serial.log"
    log "VM profile $p: $vm"

    read -r idle_avg idle_max idle_frames idle_secs <<< "$(frames "$serial" "$(summary_get "$s" idle_lines)")"
    read -r win_avg win_max _ _ <<< "$(frames "$serial" "$(summary_get "$s" run_nxwtest_lines)")"
    read -r hog_avg hog_max _ _ <<< "$(frames "$serial" "$(summary_get "$s" run_cpuhog_lines)")"
    read -r m0_free m0_user _ _ <<< "$(memprobe "$serial" 1)"
    read -r m1_free m1_user _ _ <<< "$(memprobe "$serial" 2)"
    read -r m2_free m2_user _ m2_level <<< "$(memprobe "$serial" 3)"
    delta1="-"; delta2="-"
    [ -n "${m0_free:-}" ] && [ -n "${m1_free:-}" ] && delta1=$(( (m0_free - m1_free) / 1024 ))
    [ -n "${m0_free:-}" ] && [ -n "${m2_free:-}" ] && delta2=$(( (m0_free - m2_free) / 1024 ))
    desktop_up=yes
    [ "$(summary_get "$s" system_down)" = "yes" ] && desktop_up=no

    {
        echo "## VM \`$p\`: $cpus vCPU, $mem RAM ($(summary_get "$s" accel)) - $vm"
        echo
        echo "| Measure | Value |"
        echo "|---|---|"
        echo "| Boot to desktop | $(summary_get "$s" boot_ready_s) s |"
        echo "| Idle host CPU (QEMU, 30 s) | $(summary_get "$s" idle_host_cpu_pct)% |"
        echo "| Idle desktop frames | ${idle_frames:-?} drawn in ${idle_secs:-?} s, draw avg ${idle_avg:-?} ms, max ${idle_max:-?} ms |"
        echo "| Frame draw with nxwtest's windows open | avg ${win_avg:-?} ms, max ${win_max:-?} ms |"
        echo "| Frame draw under cpuhog | avg ${hog_avg:-?} ms, max ${hog_max:-?} ms |"
        echo "| Free memory at boot | $(( ${m0_free:-0} / 1024 )) MiB (user processes $(( ${m0_user:-0} / 1024 )) MiB) |"
        echo "| Free memory lost after the tests (nxuitest's window is still open) | $delta1 MiB |"
        if [ "$p" = low ]; then
            echo "| Free memory lost after memstress | $delta2 MiB (level ${m2_level:-?}) |"
        else
            echo "| Free memory lost by the end (cpuhog) | $delta2 MiB (level ${m2_level:-?}) |"
        fi
        echo "| Desktop survived everything | $desktop_up |"
        echo "| User-process faults (several are on purpose) | $(summary_get "$s" user_faults) |"
        echo
        echo "| Test | Result | Seconds |"
        echo "|---|---|---|"
        grep -E '^run_[A-Za-z0-9_.-]+=' "$s" | grep -v '_s=\|_lines=' | while IFS='=' read -r k v; do
            n="${k#run_}"
            echo "| $n | $v | $(summary_get "$s" "run_${n}_s") |"
        done
        echo
    } >> "$REPORT"
done

{
    echo "Overall: $([ $STATUS -eq 0 ] && echo PASS || echo FAIL)."
    echo
    echo "Logs: \`$OUT\` (serial logs in vm-*/serial.log)."
} >> "$REPORT"
log "report: $REPORT"
exit $STATUS
