#!/bin/bash
#
# NeolyxOS - check syscall_table.def and syscall.h against
# docs/kernel/syscall-registry.md. Run by CI on every pull request.
#
# Fails when:
#   - a number is registered twice in syscall_table.def;
#   - a registered number is >= SYS_MAX, or SYS_MAX != NX_SYSCALL_MAX;
#   - a number is registered (or defined in syscall.h) in a range the
#     registry lists as unallocated, or in no range at all;
#   - a number the registry lists as "in use" is not registered;
#   - two SYS_* defines in syscall.h share a number.
#
# Usage: scripts/check_syscall_registry.sh
# Exit status: 0 when consistent, 1 otherwise.
#
# Copyright (c) 2025 KetiveeAI

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEF="$ROOT/kernel/include/core/syscall_table.def"
HDR="$ROOT/kernel/include/core/syscall.h"
ABI="$ROOT/kernel/include/core/nx_abi.h"
REG="$ROOT/docs/kernel/syscall-registry.md"

for f in "$DEF" "$HDR" "$ABI" "$REG"; do
    [ -f "$f" ] || { echo "[REGISTRY] ERROR: $f not found"; exit 1; }
done

ERR=0
err() { echo "[REGISTRY] ERROR: $*"; ERR=1; }

SYS_MAX=$(awk '$1 == "#define" && $2 == "SYS_MAX" { print $3; exit }' "$HDR")
NX_MAX=$(awk '$1 == "#define" && $2 == "NX_SYSCALL_MAX" { print $3; exit }' "$ABI")
[ -n "$SYS_MAX" ] || err "SYS_MAX not found in syscall.h"
[ -n "$NX_MAX" ] || err "NX_SYSCALL_MAX not found in nx_abi.h"
[ "$SYS_MAX" = "$NX_MAX" ] || err "SYS_MAX ($SYS_MAX) != NX_SYSCALL_MAX ($NX_MAX)"

# Registered numbers: NX_SYSCALL(num, "name", ...) at the start of a line
REGISTERED=$(sed -n 's/^[[:space:]]*NX_SYSCALL([[:space:]]*\([0-9][0-9]*\)[[:space:]]*,.*/\1/p' "$DEF")
[ -n "$REGISTERED" ] || err "no NX_SYSCALL lines found in syscall_table.def"
DUPS=$(echo "$REGISTERED" | sort -n | uniq -d)
[ -z "$DUPS" ] || err "registered more than once in syscall_table.def: $(echo $DUPS)"

# Numbers defined in syscall.h's number section (SYS_* up to SYS_MAX)
DEFINED=$(awk '$1 == "#define" && $2 ~ /^SYS_/ && $2 != "SYS_MAX" && $3 ~ /^[0-9]+$/ { print $3 }
               $2 == "SYS_MAX" { exit }' "$HDR")
HDUPS=$(echo "$DEFINED" | sort -n | uniq -d)
[ -z "$HDUPS" ] || err "SYS_* numbers defined more than once in syscall.h: $(echo $HDUPS)"

# Registry rows: "| a-b | owner | use | state |" in the "Syscall numbers" table
ROWS=$(sed -n '/^## Syscall numbers/,/^## /p' "$REG" \
       | awk -F'|' '/^\| *[0-9]/ {
             gsub(/ /, "", $2); owner = $3; state = $5
             gsub(/^ +| +$/, "", owner); gsub(/^ +| +$/, "", state)
             n = split($2, r, "-"); lo = r[1]; hi = (n > 1) ? r[2] : r[1]
             print lo, hi, (owner == "unallocated" ? "free" : "owned"), (state ~ /^in use/ ? "inuse" : "other")
         }')
[ -n "$ROWS" ] || err "no rows found in the registry's Syscall numbers table"

row_of() {  # prints "kind state" for a number, or nothing
    echo "$ROWS" | awk -v x="$1" '$1 <= x && x <= $2 { print $3, $4; exit }'
}

for n in $REGISTERED; do
    [ -n "$SYS_MAX" ] && [ "$n" -ge "$SYS_MAX" ] && err "syscall $n is >= SYS_MAX ($SYS_MAX)"
    r=$(row_of "$n")
    case "$r" in
        "")      err "syscall $n is registered but in no registry row" ;;
        free*)   err "syscall $n is registered in an unallocated range: ask S10 to allocate it first" ;;
    esac
done
for n in $DEFINED; do
    r=$(row_of "$n")
    case "$r" in
        "")      err "SYS_* define $n in syscall.h is in no registry row" ;;
        free*)   err "SYS_* define $n in syscall.h is in an unallocated range" ;;
    esac
done

# Every number in an "in use" row must be registered
while read -r lo hi kind state; do
    [ "$state" = inuse ] || continue
    for ((n = lo; n <= hi; n++)); do
        echo "$REGISTERED" | grep -qx "$n" \
            || err "registry lists $n as in use, but syscall_table.def does not register it"
    done
done <<< "$ROWS"

COUNT=$(echo "$REGISTERED" | grep -c .)
if [ $ERR -eq 0 ]; then
    echo "[REGISTRY] OK: $COUNT syscalls registered, all inside allocated ranges; SYS_MAX $SYS_MAX"
fi
exit $ERR
