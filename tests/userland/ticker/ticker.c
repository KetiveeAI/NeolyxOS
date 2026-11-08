/*
 * NeolyxOS Ticker - runs alongside the desktop
 *
 * A small user program used to verify multi-process support:
 *   - It is linked at the same address as the desktop (0x1000000). It can
 *     only run without corrupting the desktop if each process has its own
 *     address space.
 *   - It grows its heap (malloc on brk) and checks the memory it gets.
 *   - It prints a heartbeat once a second with sleep, so the desktop
 *     keeps rendering between ticks, then exits cleanly.
 *
 * Built on nxlibc (sdk/libc): main() instead of _start, printf to the
 * debug log, malloc instead of raw brk, nxsys_* for the native calls.
 *
 * Launched from the desktop's app drawer (/apps/ticker on the boot disk)
 * or with nxsys_spawn("/apps/ticker", ...).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <nx/sys.h>

#define TICKER_BEATS    8
#define TICKER_HEAP     (64 * 1024)

/* Lives in this program's .data, at an address the desktop also uses */
static volatile uint64_t g_marker = 0x5449434B45520001ULL;

int main(int argc, char **argv) {
    unsigned long pid = (unsigned long)nxsys_getpid();

    printf("[TICKER] PID %lu started, argc=%d", pid, argc);
    if (argc > 0 && argv && argv[0]) printf(" argv[0]=%s", argv[0]);
    printf("\n");

    printf("[TICKER] code at 0x%016llX, data at 0x%016llX (desktop uses the same range)\n",
           (unsigned long long)(uintptr_t)&main, (unsigned long long)(uintptr_t)&g_marker);

    /* Heap: grow it and check every byte we were given */
    volatile uint8_t *heap = malloc(TICKER_HEAP);
    if (!heap) {
        printf("[TICKER] FAIL: the heap did not grow\n");
        return 2;
    }
    for (uint64_t i = 0; i < TICKER_HEAP; i++) heap[i] = (uint8_t)(i * 7 + pid);
    for (uint64_t i = 0; i < TICKER_HEAP; i++) {
        if (heap[i] != (uint8_t)(i * 7 + pid)) {
            printf("[TICKER] FAIL: heap mismatch at 0x%016llX\n", (unsigned long long)(uintptr_t)&heap[i]);
            return 3;
        }
    }
    printf("[TICKER] heap 0x%016llX +64KB ok\n", (unsigned long long)(uintptr_t)heap);
    free((void *)heap);

    for (int beat = 1; beat <= TICKER_BEATS; beat++) {
        printf("[TICKER] PID %lu tick %d/%d at %llu ms\n", pid, beat, TICKER_BEATS,
               (unsigned long long)nxsys_time());
        g_marker++;
        nxsys_sleep(1000);
    }

    if (g_marker != 0x5449434B45520001ULL + TICKER_BEATS) {
        printf("[TICKER] FAIL: private data changed under us\n");
        return 4;
    }

    printf("[TICKER] PID %lu finished, exiting\n", pid);
    return 0;
}
