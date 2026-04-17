/*
 * NeolyxOS - NXGame API (Userspace IPC Bridge)
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/nxgame_api.h"

int nxgame_init(void) {
    return (int)_nxgame_syscall_0(SYS_NXGAME_INIT);
}

void* nxgame_request_fb(void) {
    return (void*)_nxgame_syscall_0(SYS_NXGAME_REQ_FB);
}

int nxgame_get_resolution(nxgame_res_t* res) {
    if (!res) return -1;
    return (int)_nxgame_syscall_1(SYS_NXGAME_GET_RES, (long)res);
}

void nxgame_swap_buffers(void) {
    _nxgame_syscall_0(SYS_NXGAME_SWAP_BUFS);
}

void nxgame_present(void) {
    _nxgame_syscall_0(SYS_NXGAME_PRESENT);
}

void nxgame_wait_vsync(void) {
    _nxgame_syscall_0(SYS_NXGAME_WAIT_VSYNC);
}
