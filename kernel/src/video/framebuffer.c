/*
 * NeolyxOS Framebuffer Globals
 * 
 * Global framebuffer variables used across the kernel.
 * These are typically set by the UEFI bootloader.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include <stdint.h>

/* Framebuffer global variables - DEFINED HERE, externed elsewhere */
/* Legacy naming - used by gpu.c, font.c, nxdisplay_kdrv.c */
volatile uint32_t *framebuffer = 0;
uint32_t fb_width = 0;
uint32_t fb_height = 0;
uint32_t fb_pitch = 0;

/* Modern naming - used by syscall.c, main.c */
uint64_t g_framebuffer_addr = 0;
uint32_t g_framebuffer_width = 0;
uint32_t g_framebuffer_height = 0;
uint32_t g_framebuffer_pitch = 0;

/* Set framebuffer parameters (updates both sets) */
void fb_set_params(uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch) {
    /* Legacy globals */
    framebuffer = (volatile uint32_t *)addr;
    fb_width = width;
    fb_height = height;
    fb_pitch = pitch;
    
    /* Modern globals */
    g_framebuffer_addr = addr;
    g_framebuffer_width = width;
    g_framebuffer_height = height;
    g_framebuffer_pitch = pitch;
}

/* Get framebuffer address */
volatile uint32_t *fb_get_address(void) {
    return framebuffer;
}
