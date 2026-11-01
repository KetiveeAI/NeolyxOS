#!/bin/bash
#
# NeolyxOS - read and check config/image-apps.list.
#
# Source this file, then call:
#   image_apps_load <repo root>
# On success it fills three parallel arrays, one entry per program:
#   IMAGE_APP_DIRS   build folder (relative to the root)
#   IMAGE_APP_FILES  built file name in that folder
#   IMAGE_APP_DESTS  absolute path on the boot image
# and returns 0. On any bad line it prints the line number and the reason
# to stderr and returns 1, so a typo never builds a half-filled image.
#
# Copyright (c) 2025 KetiveeAI

image_apps_load() {
    local root="$1"
    local list="$root/config/image-apps.list"
    IMAGE_APP_DIRS=()
    IMAGE_APP_FILES=()
    IMAGE_APP_DESTS=()

    if [ -z "$root" ] || [ ! -d "$root" ]; then
        echo "[IMAGE-APPS] ERROR: repository root '$root' not found" >&2
        return 1
    fi
    if [ ! -f "$list" ]; then
        echo "[IMAGE-APPS] ERROR: $list not found" >&2
        return 1
    fi

    local n=0 line dir file dest extra d
    local -A seen=()
    while IFS= read -r line || [ -n "$line" ]; do
        n=$((n + 1))
        line="${line%%#*}"
        line="${line//$'\r'/}"
        # shellcheck disable=SC2086
        set -- $line
        [ $# -eq 0 ] && continue
        if [ $# -ne 3 ]; then
            echo "[IMAGE-APPS] ERROR: $list:$n: expected 3 fields, got $#" >&2
            return 1
        fi
        dir="$1"; file="$2"; dest="$3"
        for d in "$dir" "$file" "$dest"; do
            case "$d" in
                *[!A-Za-z0-9_./-]*) echo "[IMAGE-APPS] ERROR: $list:$n: bad character in '$d'" >&2; return 1 ;;
                *..*)               echo "[IMAGE-APPS] ERROR: $list:$n: '..' not allowed in '$d'" >&2; return 1 ;;
            esac
        done
        case "$dir" in /*) echo "[IMAGE-APPS] ERROR: $list:$n: build folder must be relative: '$dir'" >&2; return 1 ;; esac
        case "$file" in */*) echo "[IMAGE-APPS] ERROR: $list:$n: built file must be a plain name: '$file'" >&2; return 1 ;; esac
        case "$dest" in
            /apps/?*|/System/?*) ;;
            *) echo "[IMAGE-APPS] ERROR: $list:$n: image path must start with /apps/ or /System/: '$dest'" >&2; return 1 ;;
        esac
        case "$dest" in */) echo "[IMAGE-APPS] ERROR: $list:$n: image path must name a file: '$dest'" >&2; return 1 ;; esac
        if [ ! -d "$root/$dir" ]; then
            echo "[IMAGE-APPS] ERROR: $list:$n: build folder '$dir' does not exist" >&2
            return 1
        fi
        if [ -n "${seen[$dest]:-}" ]; then
            echo "[IMAGE-APPS] ERROR: $list:$n: '$dest' already listed on line ${seen[$dest]}" >&2
            return 1
        fi
        seen[$dest]=$n
        IMAGE_APP_DIRS+=("$dir")
        IMAGE_APP_FILES+=("$file")
        IMAGE_APP_DESTS+=("$dest")
    done < "$list"

    if [ ${#IMAGE_APP_DESTS[@]} -eq 0 ]; then
        echo "[IMAGE-APPS] ERROR: $list lists no programs" >&2
        return 1
    fi
    return 0
}

# Create every parent folder of an image path inside a FAT image (mtools).
# Usage: image_apps_mkdirs <image> </path/on/image/file>
image_apps_mkdirs() {
    local img="$1" path="$2" parent="" part
    local IFS='/'
    for part in ${path%/*}; do
        [ -z "$part" ] && continue
        parent="$parent/$part"
        if ! mdir -i "$img" "::$parent" >/dev/null 2>&1; then
            mmd -i "$img" "::$parent" || return 1
        fi
    done
    return 0
}
