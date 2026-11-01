/*
 * nxlibc - NeolyxOS native system calls (<nx/sys.h>), x86_64.
 *
 * Mechanism (kernel/include/core/syscall.h, kernel/src/arch/x86_64/arch.S):
 * SYSCALL with the number in RAX and arguments in RDI, RSI, RDX, R10, R8.
 * The kernel returns the result in RAX and preserves every register except
 * RAX, RCX and R11 (and saves/restores the FPU/SSE state).
 *
 * This file is the only place nxlibc enters the kernel. The host test
 * build replaces it with tests/host/libc_host_sys.c.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <nx/sys.h>

static inline long sc0(long n) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory");
    return r;
}

static inline long sc1(long n, long a1) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return r;
}

static inline long sc2(long n, long a1, long a2) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return r;
}

static inline long sc3(long n, long a1, long a2, long a3) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return r;
}

static inline long sc5(long n, long a1, long a2, long a3, long a4, long a5) {
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return r;
}

#define P(x) ((long)(uintptr_t)(x))

long nxsys_call(long num, long a1, long a2, long a3, long a4, long a5) {
    return sc5(num, a1, a2, a3, a4, a5);
}

/* ABI */
long nxsys_abi_query(nxsys_abi_info_t *info) { return sc1(NXSYS_ABI_QUERY, P(info)); }

/* Process */
_Noreturn void nxsys_exit(int code) {
    sc1(NXSYS_EXIT, code);
    /* The kernel never returns from exit; if it did, do not run on */
    for (;;) {
        __asm__ volatile("pause");
    }
}
long     nxsys_sleep(uint32_t ms) { return sc1(NXSYS_SLEEP, (long)ms); }
uint64_t nxsys_time(void) { return (uint64_t)sc0(NXSYS_TIME); }
long     nxsys_yield(void) { return sc0(NXSYS_YIELD); }
long     nxsys_getpid(void) { return sc0(NXSYS_GETPID); }
long     nxsys_getppid(void) { return sc0(NXSYS_GETPPID); }
long     nxsys_fork(void) { return sc0(NXSYS_FORK); }
long nxsys_exec(const char *path, char *const argv[], char *const envp[]) {
    return sc3(NXSYS_EXEC, P(path), P(argv), P(envp));
}
long nxsys_wait(long pid, int *status) { return sc2(NXSYS_WAIT, pid, P(status)); }
long nxsys_brk(void *addr) { return sc1(NXSYS_BRK, P(addr)); }
long nxsys_spawn(const char *path, char *const argv[], uint32_t flags) {
    return sc3(NXSYS_SPAWN, P(path), P(argv), (long)flags);
}

/* Files */
long nxsys_open(const char *path, uint32_t flags, uint32_t mode) {
    return sc3(NXSYS_OPEN, P(path), (long)flags, (long)mode);
}
long nxsys_close(int fd) { return sc1(NXSYS_CLOSE, fd); }
long nxsys_read(int fd, void *buf, size_t count) { return sc3(NXSYS_READ, fd, P(buf), (long)count); }
long nxsys_write(int fd, const void *buf, size_t count) {
    return sc3(NXSYS_WRITE, fd, P(buf), (long)count);
}
long nxsys_seek(int fd, int64_t offset, int whence) { return sc3(NXSYS_SEEK, fd, offset, whence); }
long nxsys_stat(const char *path, nxsys_stat_t *st) { return sc2(NXSYS_STAT, P(path), P(st)); }
long nxsys_readdir(const char *path, nxsys_dirent_t *entries, uint32_t count, uint32_t *nread) {
    return sc5(NXSYS_READDIR, P(path), P(entries), (long)count, P(nread), 0);
}
long nxsys_mkdir(const char *path, uint32_t mode) { return sc2(NXSYS_MKDIR, P(path), (long)mode); }
long nxsys_rmdir(const char *path) { return sc1(NXSYS_RMDIR, P(path)); }
long nxsys_chdir(const char *path) { return sc1(NXSYS_CHDIR, P(path)); }
long nxsys_getcwd(char *buf, size_t size) { return sc2(NXSYS_GETCWD, P(buf), (long)size); }
long nxsys_rename(const char *oldpath, const char *newpath) {
    return sc2(NXSYS_RENAME, P(oldpath), P(newpath));
}

/* Sockets */
long nxsys_socket(int type, int protocol) { return sc2(NXSYS_SOCKET, type, protocol); }
long nxsys_bind(int fd, const nxsys_sockaddr_in_t *addr) { return sc2(NXSYS_BIND, fd, P(addr)); }
long nxsys_listen(int fd, int backlog) { return sc2(NXSYS_LISTEN, fd, backlog); }
long nxsys_accept(int fd, nxsys_sockaddr_in_t *addr) { return sc2(NXSYS_ACCEPT, fd, P(addr)); }
long nxsys_connect(int fd, const nxsys_sockaddr_in_t *addr) { return sc2(NXSYS_CONNECT, fd, P(addr)); }
long nxsys_send(int fd, const void *buf, size_t len) { return sc3(NXSYS_SEND, fd, P(buf), (long)len); }
long nxsys_recv(int fd, void *buf, size_t len) { return sc3(NXSYS_RECV, fd, P(buf), (long)len); }
long nxsys_closesock(int fd) { return sc1(NXSYS_CLOSESOCK, fd); }

/* System */
long nxsys_info(nxsys_sysinfo_t *info) { return sc1(NXSYS_INFO, P(info)); }
long nxsys_reboot(int cmd) { return sc1(NXSYS_REBOOT, cmd); }
long nxsys_shutdown(void) { return sc0(NXSYS_SHUTDOWN); }

/* NXGame */
long nxsys_nxgame_create(int type, uint32_t caps, const void *quotas) {
    return sc3(NXSYS_NXGAME_CREATE, type, (long)caps, P(quotas));
}
long nxsys_nxgame_destroy(int s) { return sc1(NXSYS_NXGAME_DESTROY, s); }
long nxsys_nxgame_start(int s) { return sc1(NXSYS_NXGAME_START, s); }
long nxsys_nxgame_stop(int s) { return sc1(NXSYS_NXGAME_STOP, s); }
long nxsys_nxgame_gpu_acquire(int s) { return sc1(NXSYS_NXGAME_GPU_ACQUIRE, s); }
long nxsys_nxgame_gpu_release(int s) { return sc1(NXSYS_NXGAME_GPU_RELEASE, s); }
long nxsys_nxgame_gpu_present(int s, int wait_vsync) { return sc2(NXSYS_NXGAME_GPU_PRESENT, s, wait_vsync); }
long nxsys_nxgame_audio(int s, int cmd, uint32_t rate, uint32_t channels, uint32_t frames) {
    return sc5(NXSYS_NXGAME_AUDIO, s, cmd, (long)rate, (long)channels, (long)frames);
}
long nxsys_nxgame_input(int s, void *state) { return sc2(NXSYS_NXGAME_INPUT, s, P(state)); }
long nxsys_nxgame_get_buffer(int s, void *info) { return sc2(NXSYS_NXGAME_GET_BUFFER, s, P(info)); }

/* Framebuffer, input, debug, cursor */
long nxsys_fb_map(nxsys_fb_info_t *info) { return sc1(NXSYS_FB_MAP, P(info)); }
long nxsys_fb_info(nxsys_fb_info_t *info) { return sc1(NXSYS_FB_INFO, P(info)); }
long nxsys_fb_flip(const void *backbuffer) { return sc1(NXSYS_FB_FLIP, P(backbuffer)); }
long nxsys_input_poll(nxsys_input_event_t *events, uint32_t max) {
    return sc2(NXSYS_INPUT_POLL, P(events), (long)max);
}
long nxsys_input_register(uint32_t types) { return sc1(NXSYS_INPUT_REGISTER, (long)types); }
long nxsys_debug_print(const char *msg) { return sc1(NXSYS_DEBUG_PRINT, P(msg)); }
long nxsys_cursor_set(int x, int y, int visible) { return sc3(NXSYS_CURSOR_SET, x, y, visible); }

/* RTC */
long     nxsys_rtc_get(nxsys_rtc_time_t *t) { return sc1(NXSYS_RTC_GET, P(t)); }
long     nxsys_rtc_set(const nxsys_rtc_time_t *t) { return sc1(NXSYS_RTC_SET, P(t)); }
uint64_t nxsys_rtc_unix(void) { return (uint64_t)sc0(NXSYS_RTC_UNIX); }

/* Handles, shared memory, IPC */
long nxsys_handle_close(nxsys_handle_t h) { return sc1(NXSYS_HANDLE_CLOSE, h); }
long nxsys_shm_create(uint64_t size, uint32_t flags) { return sc2(NXSYS_SHM_CREATE, (long)size, (long)flags); }
long nxsys_shm_map(nxsys_handle_t h, uint32_t prot, uint64_t *out_size) {
    return sc3(NXSYS_SHM_MAP, h, (long)prot, P(out_size));
}
long nxsys_shm_unmap(void *addr) { return sc1(NXSYS_SHM_UNMAP, P(addr)); }
long nxsys_chan_create(nxsys_handle_t out[2], uint32_t flags) {
    return sc2(NXSYS_CHAN_CREATE, P(out), (long)flags);
}
long nxsys_chan_send(nxsys_handle_t h, const nxsys_chan_msg_t *msg) { return sc2(NXSYS_CHAN_SEND, h, P(msg)); }
long nxsys_chan_recv(nxsys_handle_t h, nxsys_chan_msg_t *msg) { return sc2(NXSYS_CHAN_RECV, h, P(msg)); }
long nxsys_obj_wait(nxsys_wait_item_t *items, uint32_t count, uint64_t timeout_ms) {
    return sc3(NXSYS_OBJ_WAIT, P(items), (long)count, (long)timeout_ms);
}
long nxsys_fb_flip_rects(const void *backbuffer, const nxsys_fb_rect_t *rects, uint32_t count) {
    return sc3(NXSYS_FB_FLIP_RECTS, P(backbuffer), P(rects), (long)count);
}
long nxsys_handle_dup(nxsys_handle_t h, uint32_t rights) { return sc2(NXSYS_HANDLE_DUP, h, (long)rights); }
long nxsys_port_listen(const char *name, uint32_t flags) { return sc2(NXSYS_PORT_LISTEN, P(name), (long)flags); }
long nxsys_port_connect(const char *name, uint32_t flags) { return sc2(NXSYS_PORT_CONNECT, P(name), (long)flags); }
long nxsys_port_accept(nxsys_handle_t port, uint32_t *out_pid) { return sc2(NXSYS_PORT_ACCEPT, port, P(out_pid)); }

/* App classes and memory */
long nxsys_proc_set_class(uint32_t pid, uint32_t cls) { return sc2(NXSYS_PROC_SET_CLASS, (long)pid, (long)cls); }
long nxsys_mem_pressure_open(uint32_t flags) { return sc1(NXSYS_MEM_PRESSURE_OPEN, (long)flags); }
long nxsys_mem_pressure_read(nxsys_handle_t h) { return sc1(NXSYS_MEM_PRESSURE_READ, h); }
long nxsys_mem_stats(nxsys_mem_stats_t *st, uint32_t size) { return sc2(NXSYS_MEM_STATS, P(st), (long)size); }

/* Network status */
long nxsys_net_if_list(nxsys_net_if_info_t *buf, uint32_t cap, uint32_t elem_size) {
    return sc3(NXSYS_NET_IF_LIST, P(buf), (long)cap, (long)elem_size);
}
