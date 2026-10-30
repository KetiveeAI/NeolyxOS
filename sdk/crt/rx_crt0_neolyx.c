/*
 * NeolyxOS REOX SDK - process entry for REOX applications
 *
 * The kernel enters a userspace ELF at `_start` (desktop/user.ld) with a
 * valid stack and a pre-mapped heap.  reoxc emits `int main(void)` for
 * `fn main() -> int` (and for `fn main()`, returning 0); this file calls it
 * and exits with its status.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "nxsyscall.h"

extern int main(void);

static void rx_crt_halt(void) {
    for (;;) {
        __asm__ volatile("pause");
    }
}

void _start(void) {
    nx_debug_print("[REOX] app start\n");

    int status = main();

    nx_debug_print(status == 0 ? "[REOX] app exit 0\n" : "[REOX] app exit nonzero\n");
    nx_exit(status);
    rx_crt_halt();
}
