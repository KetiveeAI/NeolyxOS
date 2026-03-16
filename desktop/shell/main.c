/*
 * NeolyxOS Desktop - Userspace Entry Point
 * 
 * This is the main entry point for the desktop environment.
 * Runs in userspace, communicates with kernel via syscalls.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../include/nxsyscall.h"
#include "../include/desktop_shell.h"

/* Main entry point - called from _start */
void _start(void) {
    nx_debug_print("[DESKTOP_MAIN] Starting...\n");
    
    /* Framebuffer initialization verified - get FB info for desktop_init */
    fb_info_t fb;
    if (nx_fb_info(&fb) != 0 || fb.base_addr == 0) {
        nx_debug_print("[DESKTOP_MAIN] Failed to get FB info\n");
        while (1) { __asm__ volatile("pause"); }
    }
    
    /* Initialize desktop with 0 params = use syscall to get FB */
    nx_debug_print("[DESKTOP_MAIN] Calling desktop_init...\n");
    if (desktop_init(0, 0, 0, 0) < 0) {
        nx_debug_print("[DESKTOP_MAIN] desktop_init FAILED\n");
        /* Failed - halt */
        while (1) {
            __asm__ volatile("pause");  /* Ring 3 safe */
        }
    }
    nx_debug_print("[DESKTOP_MAIN] desktop_init returned OK\n");
    nx_debug_print("[DESKTOP_MAIN] Calling desktop_run...\n");
    
    /* Run desktop event loop - this polls mouse/keyboard input */
    desktop_run();
    
    /* If we get here, desktop exited - shutdown */
    nx_exit(0);
    
    /* Should never reach here */
    while (1) {
        __asm__ volatile("pause");  /* Ring 3 safe */
    }
}
