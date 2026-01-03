/*
 * NeolyxOS Kernel - Splash Header
 * 
 * Boot splash screen interface
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYXOS_SPLASH_H
#define NEOLYXOS_SPLASH_H

#include <stdint.h>

/* Initialize splash screen */
void splash_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch);

/* Show the boot splash screen */
void splash_show(void);

/* Update progress and status */
void splash_update(uint32_t percent, const char *status);

/* Hide splash screen */
void splash_hide(void);

/* Get current progress */
uint32_t splash_get_progress(void);

#endif /* NEOLYXOS_SPLASH_H */
