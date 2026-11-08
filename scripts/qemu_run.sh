#!/bin/bash
#
# NeolyxOS - boot an image headless in QEMU, wait for the desktop, optionally
# launch test programs from the app drawer, and write a summary.
#
# Used by CI (boot smoke job), by scripts/regression.sh and by hand. It never
# changes the image it is given: it boots a private copy with the Setup
# marker added, so a test run starts on the desktop, not in Setup.
#
# Usage: scripts/qemu_run.sh [options]
#   --image FILE        image to boot (default: neolyx.img)
#   --cpus N            virtual CPUs (default: 2)
#   --mem SIZE          memory, QEMU syntax such as 1G or 4G (default: 4G)
#   --boot-timeout SEC  seconds to wait for the desktop (default: 360)
#   --run NAME[:SEC[:TEXT]]
#                       after the desktop is ready, launch /apps/NAME from the
#                       app drawer and wait up to SEC seconds (default 180)
#                       for "RESULT: PASS" (or for TEXT, a fixed string, when
#                       given: for tests whose last part needs a person).
#                       "RESULT: FAIL", "] FAIL", ": FAIL" or "(FAIL)" in
#                       its output fails the run.
#                       Repeat the option to run several programs in order.
#   --idle SEC          after boot, before any --run, stay idle SEC seconds
#                       and record the host CPU QEMU used in that time
#                       (idle_host_cpu_pct, % of all virtual CPUs)
#   --screenshot        save a PPM screendump of the final screen
#   --out DIR           output folder (default: build/qemu-run)
#   --no-net            boot without a network card
#
# Output in --out: serial.log, summary.txt (key=value lines), screen.ppm.
# summary.txt records the serial.log line range of every phase
# (idle_lines, run_NAME_lines = first-last) so callers can read the
# desktop's "[DESKTOP] Frames:" lines for each phase.
# Exit status: 0 when the desktop came up, the kernel did not halt, the
# desktop process did not exit and every --run program passed; 1 otherwise;
# 2 on bad usage. Faults in ordinary user processes are counted, not failed:
# several tests crash a child on purpose.
#
# Needs: qemu-system-x86_64, OVMF (/usr/share/ovmf/OVMF.fd or
# /usr/share/OVMF/OVMF_CODE.fd), mtools, nc (OpenBSD netcat, for -U).
#
# Copyright (c) 2025 KetiveeAI

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="$ROOT/neolyx.img"
CPUS=2
MEM=4G
BOOT_TIMEOUT=360
OUT="$ROOT/build/qemu-run"
IDLE=0
SHOT=0
NET=1
RUNS=()

READY_LINE='[DESKTOP] Initial render complete'
EXIT_WAIT=30   # seconds to wait for a --run program to exit after its result

log()  { echo "[QEMU-RUN] $*"; }
fail() { echo "[QEMU-RUN] ERROR: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --image)        [ $# -ge 2 ] || fail "--image needs a file"; IMAGE="$2"; shift 2 ;;
        --cpus)         [ $# -ge 2 ] || fail "--cpus needs a number"; CPUS="$2"; shift 2 ;;
        --mem)          [ $# -ge 2 ] || fail "--mem needs a size"; MEM="$2"; shift 2 ;;
        --boot-timeout) [ $# -ge 2 ] || fail "--boot-timeout needs seconds"; BOOT_TIMEOUT="$2"; shift 2 ;;
        --run)          [ $# -ge 2 ] || fail "--run needs a program name"; RUNS+=("$2"); shift 2 ;;
        --idle)         [ $# -ge 2 ] || fail "--idle needs seconds"; IDLE="$2"; shift 2 ;;
        --screenshot)   SHOT=1; shift ;;
        --out)          [ $# -ge 2 ] || fail "--out needs a folder"; OUT="$2"; shift 2 ;;
        --no-net)       NET=0; shift ;;
        -h|--help)      sed -n '2,30p' "$0"; exit 0 ;;
        *)              fail "unknown option: $1 (see --help)" ;;
    esac
done

# ---- Validate input ----
is_uint() { case "$1" in ''|*[!0-9]*) return 1 ;; *) return 0 ;; esac; }
is_uint "$CPUS" && [ "$CPUS" -ge 1 ] && [ "$CPUS" -le 16 ] || fail "--cpus must be 1..16"
is_uint "$BOOT_TIMEOUT" && [ "$BOOT_TIMEOUT" -ge 10 ] || fail "--boot-timeout must be >= 10"
is_uint "$IDLE" || fail "--idle must be a whole number of seconds"
case "$MEM" in *[!0-9MG]*|'') fail "--mem must look like 1024M or 4G" ;; esac
[ -f "$IMAGE" ] || fail "image not found: $IMAGE (run ./build_all.sh and ./rebuild_image.sh)"
for r in "${RUNS[@]}"; do
    name="${r%%:*}"
    case "$name" in ''|*[!A-Za-z0-9_.-]*|.*) fail "bad program name in --run: '$name'" ;; esac
    if [ "$r" != "$name" ]; then
        rest="${r#*:}"
        is_uint "${rest%%:*}" || fail "bad timeout in --run $r"
    fi
done
for tool in qemu-system-x86_64 mcopy mmd mdir nc; do
    command -v "$tool" >/dev/null 2>&1 || fail "'$tool' not found"
done
OVMF=""
for f in /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF.fd /usr/share/qemu/OVMF.fd; do
    [ -f "$f" ] && { OVMF="$f"; break; }
done
[ -n "$OVMF" ] || fail "OVMF firmware not found (install the ovmf package)"

mkdir -p "$OUT" || fail "cannot create $OUT"
SERIAL="$OUT/serial.log"
SUMMARY="$OUT/summary.txt"
WORK="$(mktemp -d)"
MON="$WORK/monitor.sock"
QEMU_PID=""

cleanup() {
    if [ -n "$QEMU_PID" ] && kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null
        sleep 1
        kill -9 "$QEMU_PID" 2>/dev/null
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

: > "$SERIAL"
: > "$SUMMARY"
summary() { echo "$1=$2" >> "$SUMMARY"; }

# ---- Private copy of the image, with the Setup marker ----
cp "$IMAGE" "$WORK/boot.img" || fail "cannot copy $IMAGE"
mdir -i "$WORK/boot.img" ::/System >/dev/null 2>&1 || mmd -i "$WORK/boot.img" ::/System
mdir -i "$WORK/boot.img" ::/System/Config >/dev/null 2>&1 || mmd -i "$WORK/boot.img" ::/System/Config
printf '# NeolyxOS first-run setup marker (written by scripts/qemu_run.sh)\n[setup]\n' \
    | mcopy -o -i "$WORK/boot.img" - ::/System/Config/setup_complete.nlc \
    || fail "cannot write the Setup marker into the image copy"

# ---- Boot ----
ACCEL=(-accel tcg)
ACCEL_NAME=tcg
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    ACCEL=(-enable-kvm -cpu host)
    ACCEL_NAME=kvm
fi
NET_ARGS=()
[ "$NET" -eq 1 ] && NET_ARGS=(-netdev user,id=net0 -device e1000,netdev=net0)

log "booting $(basename "$IMAGE"): ${CPUS} CPU, ${MEM} RAM, accel ${ACCEL_NAME}"
summary accel "$ACCEL_NAME"
summary cpus "$CPUS"
summary mem "$MEM"

START_NS=$(date +%s%N)
qemu-system-x86_64 "${ACCEL[@]}" -bios "$OVMF" \
    -machine usb=off \
    -device ahci,id=ahci0 \
    -drive file="$WORK/boot.img",format=raw,if=none,id=boot \
    -device ide-hd,drive=boot,bus=ahci0.0 \
    -m "$MEM" -smp "$CPUS" \
    "${NET_ARGS[@]}" \
    -display none -serial file:"$SERIAL" \
    -monitor unix:"$MON",server,nowait \
    -no-reboot &
QEMU_PID=$!

elapsed_s() { echo $(( ($(date +%s%N) - START_NS) / 1000000000 )); }

mon() {
    # One monitor command; nc -N closes after stdin ends.
    printf '%s\n' "$1" | nc -U -N "$MON" >/dev/null 2>&1
}
key() { mon "sendkey $1"; sleep 0.4; }

# The kernel halted, or the desktop (PID 1) is gone: the system is down.
fatal() {
    grep -aqE 'KERNEL EXCEPTION|KERNEL PANIC|Process 1 \('"'"'desktop'"'"'\) exited' "$SERIAL"
}

alive() { kill -0 "$QEMU_PID" 2>/dev/null; }

# ---- Wait for the desktop ----
READY=0
while [ "$(elapsed_s)" -lt "$BOOT_TIMEOUT" ]; do
    alive || break
    if grep -qF "$READY_LINE" "$SERIAL"; then READY=1; break; fi
    fatal && break
    sleep 1
done
BOOT_S=$(elapsed_s)
STATUS=0
if [ "$READY" -eq 1 ]; then
    log "desktop ready after ${BOOT_S} s"
    summary boot_ready_s "$BOOT_S"
else
    log "desktop NOT ready after ${BOOT_S} s"
    summary boot_ready_s none
    STATUS=1
fi
summary abi_line "\"$(grep -m1 '^\[SYSCALL\] NeolyxOS ABI' "$SERIAL" | tr -d '\r"')\""

# ---- Idle measurement (before any program runs) ----
# Host CPU time of the QEMU process (utime + stime, in clock ticks) over
# the idle window, as a share of all virtual CPUs.
qemu_cpu_ticks() {
    awk '{ print $14 + $15 }' "/proc/$QEMU_PID/stat" 2>/dev/null
}
if [ "$IDLE" -gt 0 ] && [ "$READY" -eq 1 ] && alive; then
    log "idle for ${IDLE} s"
    sleep 3
    l0=$(wc -l < "$SERIAL")
    c0=$(qemu_cpu_ticks)
    t0=$(date +%s%N)
    sleep "$IDLE"
    c1=$(qemu_cpu_ticks)
    t1=$(date +%s%N)
    l1=$(wc -l < "$SERIAL")
    hz=$(getconf CLK_TCK)
    if [ -n "$c0" ] && [ -n "$c1" ]; then
        pct=$(awk -v c="$((c1 - c0))" -v hz="$hz" -v ns="$((t1 - t0))" -v n="$CPUS" \
              'BEGIN { printf "%.1f", 100 * (c / hz) / (ns / 1e9) / n }')
        summary idle_host_cpu_pct "$pct"
        log "idle: QEMU used ${pct}% of ${CPUS} virtual CPU(s) on the host"
    fi
    summary idle_s "$IDLE"
    summary idle_lines "$((l0 + 1))-$l1"
fi

# ---- Launch programs from the app drawer ----
# The drawer lists /apps programs first, in directory order (one
# "[DRAWER] Found program /apps/NAME" line each). Its selection is kept
# between openings and starts at -1, so: Right once (-1 -> 0), Left enough
# times to reach 0, then Right to the program's index, then Enter.
drawer_index() {
    grep -a '^\[DRAWER\] Found program /apps/' "$SERIAL" | tr -d '\r' \
        | awk -v want="/apps/$1" '{ if ($4 == want) { print NR - 1; exit } }'
}

launch() {
    local name="$1" idx count i tries before
    idx=$(drawer_index "$name")
    [ -n "$idx" ] || { log "/apps/$name is not in the app drawer"; return 1; }
    count=$(grep -ac '^\[DRAWER\] Found program /apps/' "$SERIAL")
    for tries in 1 2 3; do
        before=$(grep -ac "^\[DRAWER\] Launching /apps/" "$SERIAL")
        key meta_l
        sleep 4
        key right
        for ((i = 0; i < count + 2; i++)); do key left; done
        for ((i = 0; i < idx; i++)); do key right; done
        key ret
        sleep 3
        local last
        last=$(grep -a "^\[DRAWER\] Launching /apps/" "$SERIAL" | tail -n +$((before + 1)) | tail -1 | tr -d '\r')
        if [ "$last" = "[DRAWER] Launching /apps/$name" ]; then return 0; fi
        log "launch attempt $tries for $name did not start it (${last:-nothing launched}); retrying"
        key esc
        sleep 2
    done
    return 1
}

for r in "${RUNS[@]}"; do
    [ "$READY" -eq 1 ] || break
    alive || { STATUS=1; break; }
    name="${r%%:*}"
    limit=180
    want='RESULT: PASS'
    if [ "$r" != "$name" ]; then
        rest="${r#*:}"
        limit="${rest%%:*}"
        [ "$rest" != "$limit" ] && want="${rest#*:}"
    fi
    lines_before=$(wc -l < "$SERIAL")
    if ! launch "$name"; then
        summary "run_$name" not_launched
        STATUS=1
        continue
    fi
    t0=$(date +%s)
    result=timeout
    while [ $(( $(date +%s) - t0 )) -lt "$limit" ]; do
        new=$(tail -n +"$((lines_before + 1))" "$SERIAL" | tr -d '\r')
        if echo "$new" | grep -qE 'RESULT: FAIL|\] FAIL( |$)|\(FAIL\)|: FAIL'; then result=fail; break; fi
        if echo "$new" | grep -qF -- "$want"; then result=pass; break; fi
        alive || { result=qemu_exited; break; }
        fatal && { result=system_down; break; }
        sleep 2
    done
    # Let the program finish before the next one starts, so its last lines
    # (often its RESULT line after a FAIL) are not read as the next
    # program's output. Programs that keep a window open never exit: give
    # up after EXIT_WAIT seconds.
    # The desktop (PID 1) is the parent of the launched program; its own
    # children may share its name.
    t1=$(date +%s)
    pid=$(tail -n +"$((lines_before + 1))" "$SERIAL" | tr -d '\r' \
          | sed -n "s/^\[PROC\] Spawned '$name' PID \([0-9][0-9]*\) (parent 1).*/\1/p" | head -1)
    while [ -n "$pid" ] && [ $(( $(date +%s) - t1 )) -lt "$EXIT_WAIT" ]; do
        tail -n +"$((lines_before + 1))" "$SERIAL" | tr -d '\r' \
            | grep -qE "^\[PROC\] Process $pid \('$name'\) exited" && break
        alive || break
        sleep 1
    done
    log "$name: $result ($(( t1 - t0 )) s)"
    summary "run_${name}_lines" "$((lines_before + 1))-$(wc -l < "$SERIAL")"
    summary "run_$name" "$result"
    summary "run_${name}_s" "$(( t1 - t0 ))"
    [ "$result" = pass ] || STATUS=1
done

if [ "$SHOT" -eq 1 ] && alive; then
    mon "screendump $WORK/screen.ppm"
    sleep 2
    [ -s "$WORK/screen.ppm" ] && cp "$WORK/screen.ppm" "$OUT/screen.ppm" && log "screenshot: $OUT/screen.ppm"
fi

summary user_faults "$(grep -ac '^\[FAULT\] Vector' "$SERIAL")"
if fatal; then
    log "the system went down:"
    grep -aE -m3 -B2 -A6 'KERNEL EXCEPTION|KERNEL PANIC|Process 1 \('"'"'desktop'"'"'\) exited' "$SERIAL" | sed 's/^/    /'
    summary system_down yes
    STATUS=1
else
    summary system_down no
fi
alive || { log "QEMU exited early"; summary qemu_exited_early yes; }

summary status "$([ "$STATUS" -eq 0 ] && echo pass || echo fail)"
log "summary: $SUMMARY"
exit "$STATUS"
