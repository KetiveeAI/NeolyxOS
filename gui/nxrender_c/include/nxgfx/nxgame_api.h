/*
 * NeolyxOS - NXGame API (Userspace IPC Bridge)
 * Copyright (c) 2025 KetiveeAI
 * 
 * Production zero-mock IPC mappings for native NXGame rendering over Ring-0 syscalls.
 */

#ifndef NXGAME_API_H
#define NXGAME_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* System Call Definitions based on NXGame kernel table */
#define SYS_NXGAME_INIT      110
#define SYS_NXGAME_SWAP_BUFS 111
#define SYS_NXGAME_REQ_FB    112
#define SYS_NXGAME_GET_RES   113
#define SYS_NXGAME_PRESENT   114
#define SYS_NXGAME_BLIT      115
#define SYS_NXGAME_WAIT_VSYNC 116
#define SYS_NXGAME_GET_FPS   117
#define SYS_NXGAME_AUDIO_CFG 118
#define SYS_NXGAME_AUDIO_WR  119

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
} nxgame_res_t;

/* Syscall invocation macro for systems without libc */
static inline long _nxgame_syscall_0(long num) {
    long ret;
    __asm__ volatile("syscall" : "=a" (ret) : "a" (num) : "rcx", "r11", "memory");
    return ret;
}

static inline long _nxgame_syscall_1(long num, long arg1) {
    long ret;
    __asm__ volatile("syscall" : "=a" (ret) : "a" (num), "D" (arg1) : "rcx", "r11", "memory");
    return ret;
}

static inline long _nxgame_syscall_2(long num, long arg1, long arg2) {
    long ret;
    __asm__ volatile("syscall" : "=a" (ret) : "a" (num), "D" (arg1), "S" (arg2) : "rcx", "r11", "memory");
    return ret;
}

/* API Wrappers */

/* Initializes an NXGame session. Returns 0 on success. */
int nxgame_init(void);

/* Requests a mapped framebuffer memory address from the kernel. */
void* nxgame_request_fb(void);

/* Retrieves the current display resolution and pitch. */
int nxgame_get_resolution(nxgame_res_t* res);

/* Swaps the double-buffered frame. */
void nxgame_swap_buffers(void);

/* Submits the current backbuffer to the compositor. */
void nxgame_present(void);

/* Waits for the next vertical blank. */
void nxgame_wait_vsync(void);

#ifdef __cplusplus
}
#endif
#endif
