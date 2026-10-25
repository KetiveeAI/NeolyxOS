/*
 * NeolyxOS REOX SDK - process entry for REOX applications
 *
 * The kernel enters a userspace ELF at `_start` (desktop/user.ld) with a
 * valid stack and a pre-mapped heap.  reoxc emits `int64_t main(void)`
 * for `fn main() -> int`; this file calls it and exits with its status.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "nxsyscall.h"

extern int64_t main(void);

static void rx_crt_halt(void) {
    for (;;) {
        __asm__ volatile("pause");
    }
}

void _start(void) {
    nx_debug_print("[REOX] app start\n");

    int64_t status = main();

    if (status < -2147483647LL || status > 2147483647LL) {
        nx_debug_print("[REOX] main returned out-of-range status; clamping to 1\n");
        status = 1;
    }
    nx_debug_print(status == 0 ? "[REOX] app exit 0\n" : "[REOX] app exit nonzero\n");
    nx_exit((int)status);
    rx_crt_halt();
}
