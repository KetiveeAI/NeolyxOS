/*
 * NOTICE: KERNEL UI DIRECTORY
 * ===========================
 * 
 * This directory (kernel/src/ui/) is for KERNEL-LEVEL rendering only:
 *   - font.c       - Kernel console font rendering
 *   - nxicon.c     - System icon primitives
 * 
 * PROHIBITED FILES:
 *   - Desktop shell code
 *   - Window manager code  
 *   - User interface widgets
 *   - Any GUI compositor code
 * 
 * REASON:
 *   Operating systems must separate kernel from userspace GUI.
 *   Desktop environments run in Ring 3 (userspace), not Ring 0 (kernel).
 * 
 * CORRECT LOCATION FOR DESKTOP:
 *   /desktop/shell/         - Desktop shell implementation
 *   /desktop/include/       - Desktop headers
 *   /desktop/apps/          - Desktop applications
 * 
 * The desktop communicates with kernel via SYSCALLS (100-106):
 *   - SYS_GFX_INIT (100)    - Initialize graphics
 *   - SYS_GFX_DRAW (101)    - Draw operation
 *   - SYS_GFX_BLIT (102)    - Blit buffer
 *   - SYS_GFX_FLIP (103)    - Flip framebuffer
 *   - SYS_INPUT_POLL (104)  - Poll input events
 * 
 * See: /desktop/README.md for architecture details
 * See: /Architecture.md for full system design
 * 
 * Copyright (c) 2025 KetiveeAI
 */

/* This file intentionally contains no code - it is a notice only */
