/*
 * NeolyxOS Userspace Test - Minimal Syscall Test
 * 
 * Tests that syscalls work from Ring 3.
 * Draws a red rectangle to prove framebuffer access works.
 * 
 * Build: x86_64-elf-gcc -ffreestanding -nostdlib -O2 -o test_userspace test_userspace.c
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

/* Syscall numbers from kernel/include/core/syscall.h */
#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_FB_MAP      120
#define SYS_FB_INFO     121
#define SYS_FB_FLIP     122

/* Framebuffer info structure */
typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} fb_info_t;

/* Syscall wrappers */
static inline int64_t syscall0(int64_t num) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall1(int64_t num, int64_t arg1) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* Draw a filled rectangle */
static void draw_rect(uint32_t *fb, uint32_t pitch, 
                      int x, int y, int w, int h, uint32_t color) {
    uint32_t stride = pitch / 4;
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            fb[py * stride + px] = color;
        }
    }
}

/* Entry point - called by kernel via ELF loader */
void _start(void) {
    fb_info_t fb_info;
    
    /* Get framebuffer info */
    int ret = syscall1(SYS_FB_INFO, (int64_t)&fb_info);
    if (ret < 0) {
        /* Failed to get FB info - exit */
        syscall1(SYS_EXIT, 1);
        while(1);
    }
    
    /* Map framebuffer to our address space */
    uint32_t *fb = (uint32_t *)syscall0(SYS_FB_MAP);
    if (!fb) {
        syscall1(SYS_EXIT, 2);
        while(1);
    }
    
    /* SUCCESS! Draw a green rectangle to prove we're running in userspace */
    /* Draw "SUCCESS" pattern: Green box with white border */
    
    int cx = fb_info.width / 2;
    int cy = fb_info.height / 2;
    
    /* White border */
    draw_rect(fb, fb_info.pitch, cx - 102, cy - 52, 204, 104, 0xFFFFFFFF);
    
    /* Green fill */
    draw_rect(fb, fb_info.pitch, cx - 100, cy - 50, 200, 100, 0xFF00FF00);
    
    /* Flip to show */
    syscall0(SYS_FB_FLIP);
    
    /* Exit with success code */
    syscall1(SYS_EXIT, 0);
    
    /* Should never reach here */
    while(1) {
        __asm__ volatile("hlt");
    }
}
