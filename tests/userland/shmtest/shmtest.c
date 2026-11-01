/*
 * NeolyxOS shmtest - handles and shared memory (ABI v1.1, step 1)
 *
 * Checks syscalls 140-143 and 149 against docs/desktop/proposals/kernel-shm-ipc.md:
 *   - abi.query reports NX_ABI_FLAG_SHM
 *   - argument checks (size 0, too large, flags, prot, bad handles)
 *   - a 4 MiB object mapped twice shows the same memory at two addresses
 *   - rights: a READ|MAP duplicate cannot map writable or be duplicated
 *   - pointers into a shared mapping are accepted by other syscalls
 *   - a mapping keeps the object alive after its handle is closed
 *   - stale handles (closed, or slot reused) give -EBADF
 *   - limits: 256 handles, 128 mappings, 256 MiB quota per process
 *   - every page returns to the PMM after unmap/close, and after a child
 *     that leaks 35 MiB of shm (mapped and unmapped) exits
 *
 * Channels are the next step, so passing a buffer between two processes
 * is tested there.
 *
 * Output: "[SHMTEST] PASS/FAIL ..." lines and one final
 * "[SHMTEST] RESULT: PASS" or "[SHMTEST] RESULT: FAIL". Exit code 0 or 1.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "../../../kernel/include/core/nx_ipc.h"

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_ABI_QUERY    0
#define SYS_EXIT         1
#define SYS_SLEEP        2
#define SYS_GETPID       5
#define SYS_WAIT         9
#define SYS_SPAWN        13
#define SYS_OPEN         20
#define SYS_CLOSE        21
#define SYS_INFO         50
#define SYS_DEBUG_PRINT  127
#define SYS_HANDLE_CLOSE 140
#define SYS_SHM_CREATE   141
#define SYS_SHM_MAP      142
#define SYS_SHM_UNMAP    143
#define SYS_HANDLE_DUP   149

#define SPAWN_WAITABLE   0x1
#define VFS_O_RDONLY     0x0001

#define EBADF            9
#define ENOMEM           12
#define EACCES           13
#define EINVAL           22
#define EMFILE           24

#define MIB              (1024ULL * 1024ULL)
#define PAGE             4096ULL
#define LEAK_TOLERANCE   (64ULL * 1024ULL)  /* kernel heap may grow a little */

typedef struct {            /* nx_abi_info_t */
    uint16_t major, minor, patch, flags;
    uint32_t syscall_count;
    uint32_t reserved[3];
} abi_info_t;

typedef struct {            /* sysinfo_t */
    char os_name[32];
    char os_version[32];
    char arch[16];
    uint64_t uptime_ms;
    uint64_t total_memory;
    uint64_t free_memory;
    uint32_t num_cpus;
    uint32_t num_processes;
} sys_info_t;

static inline int64_t sc1(int64_t n, int64_t a1) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t sc2(int64_t n, int64_t a1, int64_t a2) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t sc3(int64_t n, int64_t a1, int64_t a2, int64_t a3) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return r;
}

static int64_t shm_create(uint64_t size, uint64_t flags) { return sc2(SYS_SHM_CREATE, (int64_t)size, (int64_t)flags); }
static int64_t shm_map(int64_t h, uint64_t prot, uint64_t *out) { return sc3(SYS_SHM_MAP, h, (int64_t)prot, (int64_t)out); }
static int64_t shm_unmap(int64_t addr) { return sc1(SYS_SHM_UNMAP, addr); }
static int64_t h_close(int64_t h) { return sc1(SYS_HANDLE_CLOSE, h); }
static int64_t h_dup(int64_t h, uint64_t rights) { return sc2(SYS_HANDLE_DUP, h, (int64_t)rights); }

/* ============ Output ============ */

static char g_line[192];
static int g_len;
static int g_failed;

static void out_str(const char *s) {
    while (*s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_sdec(int64_t v) {
    char buf[22];
    int i = 21;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 1) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (neg) buf[--i] = '-';
    out_str(&buf[i]);
}

static void out_hex(uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) buf[2 + i] = hex[(v >> (60 - 4 * i)) & 0xF];
    buf[18] = '\0';
    out_str(buf);
}

static void flush(void) {
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

static void finish(int code) {
    sc1(SYS_EXIT, code);
    for (;;) { __asm__ volatile("pause"); }
}

static void check(int ok, const char *what, int64_t got) {
    out_str(ok ? "[SHMTEST] PASS " : "[SHMTEST] FAIL ");
    out_str(what);
    out_str(" (got ");
    out_sdec(got);
    out_str(")\n");
    flush();
    if (!ok) g_failed = 1;
}

static void note(const char *what, uint64_t v) {
    out_str("[SHMTEST] ");
    out_str(what);
    out_sdec((int64_t)v);
    out_str("\n");
    flush();
}

/* ============ Helpers ============ */

static uint64_t free_memory(void) {
    sys_info_t info;
    if (sc1(SYS_INFO, (int64_t)&info) != 0) return 0;
    return info.free_memory;
}

static int in_shared_region(int64_t addr) {
    return addr >= 0x0000400000000000LL && addr < 0x0000408000000000LL &&
           (addr & (int64_t)(PAGE - 1)) == 0;
}

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void check_memory_back(const char *what, uint64_t baseline) {
    uint64_t now = free_memory();
    int64_t diff = (int64_t)baseline - (int64_t)now;   /* > 0: memory missing */
    check(diff <= (int64_t)LEAK_TOLERANCE, what, diff);
}

/* One full create/map/unmap/close cycle (warms the kernel heap) */
static void warm_up(void) {
    int64_t h = shm_create(4 * MIB, 0);
    if (h <= 0) return;
    int64_t a = shm_map(h, NX_SHM_PROT_READ | NX_SHM_PROT_WRITE, 0);
    if (a > 0) shm_unmap(a);
    h_close(h);
}

/* ============ Child: leaks shm and exits ============ */

static void child_main(void) {
    int64_t big = shm_create(32 * MIB, 0);
    if (big <= 0) finish(10);
    int64_t a = shm_map(big, NX_SHM_PROT_READ | NX_SHM_PROT_WRITE, 0);
    if (!in_shared_region(a)) finish(11);
    volatile uint64_t *p = (volatile uint64_t *)a;
    for (uint64_t i = 0; i < (32 * MIB) / 8; i += PAGE / 8) p[i] = i;  /* touch every page */
    for (int i = 0; i < 3; i++) {
        if (shm_create(1 * MIB, 0) <= 0) finish(12);                 /* unmapped, never closed */
    }
    finish(0);  /* no unmap, no close: the kernel must clean up */
}

/* ============ Parent ============ */

void _start(long argc, char **argv) {
    if (argc >= 2 && argv && argv[1] && streq(argv[1], "child")) child_main();
    if (argc >= 2 && argv && argv[1] && streq(argv[1], "idle")) finish(0);

    out_str("[SHMTEST] PID ");
    out_sdec(sc1(SYS_GETPID, 0));
    out_str(" started\n");
    flush();

    /* ABI capability flag */
    abi_info_t abi;
    int64_t r = sc1(SYS_ABI_QUERY, (int64_t)&abi);
    check(r == 0 && (abi.flags & NX_ABI_FLAG_SHM), "abi.query reports NX_ABI_FLAG_SHM", r == 0 ? abi.flags : r);
    if (r != 0 || !(abi.flags & NX_ABI_FLAG_SHM)) {
        out_str("[SHMTEST] RESULT: FAIL (no shared memory)\n");
        flush();
        finish(1);
    }

    warm_up();
    uint64_t baseline = free_memory();
    note("free memory baseline (bytes): ", baseline);

    /* Argument checks */
    check(shm_create(0, 0) == -EINVAL, "shm.create(0) is -EINVAL", shm_create(0, 0));
    r = shm_create(NX_SHM_SIZE_MAX + 1, 0);
    check(r == -EINVAL, "shm.create(64 MiB + 1) is -EINVAL", r);
    r = shm_create(PAGE, 1);
    check(r == -EINVAL, "shm.create with flags != 0 is -EINVAL", r);
    check(h_close(12345) == -EBADF, "handle.close of a bogus handle is -EBADF", h_close(12345));
    check(shm_map(0, NX_SHM_PROT_READ, 0) == -EBADF, "shm.map of handle 0 is -EBADF", shm_map(0, NX_SHM_PROT_READ, 0));

    /* One 4 MiB object, mapped read-write */
    int64_t h = shm_create(4 * MIB, 0);
    check(h > 0, "shm.create(4 MiB) returns a handle", h);
    if (h <= 0) { out_str("[SHMTEST] RESULT: FAIL\n"); flush(); finish(1); }

    r = shm_map(h, 0, 0);
    check(r == -EINVAL, "shm.map with prot 0 is -EINVAL", r);
    r = shm_map(h, 4, 0);
    check(r == -EINVAL, "shm.map with an unknown prot bit is -EINVAL", r);

    uint64_t size = 0;
    int64_t a1 = shm_map(h, NX_SHM_PROT_READ | NX_SHM_PROT_WRITE, &size);
    check(in_shared_region(a1), "shm.map returns an address in the shared region", a1);
    check(size == 4 * MIB, "shm.map reports the object size", (int64_t)size);
    out_str("[SHMTEST] mapped at ");
    out_hex((uint64_t)a1);
    out_str("\n");
    flush();
    if (!in_shared_region(a1)) { out_str("[SHMTEST] RESULT: FAIL\n"); flush(); finish(1); }

    volatile uint64_t *p1 = (volatile uint64_t *)a1;
    int zero = 1;
    for (uint64_t i = 0; i < (4 * MIB) / 8; i++) if (p1[i] != 0) { zero = 0; break; }
    check(zero, "new object is zero-filled", zero);
    for (uint64_t i = 0; i < (4 * MIB) / 8; i++) p1[i] = i * 0x9E3779B97F4A7C15ULL;

    /* Second mapping of the same object: same memory, read-only */
    int64_t a2 = shm_map(h, NX_SHM_PROT_READ, 0);
    check(in_shared_region(a2) && a2 != a1, "second shm.map gives a second address", a2);
    volatile uint64_t *p2 = (volatile uint64_t *)a2;
    int same = 1;
    for (uint64_t i = 0; i < (4 * MIB) / 8; i++) {
        if (p2[i] != i * 0x9E3779B97F4A7C15ULL) { same = 0; break; }
    }
    check(same, "both mappings show the same 4 MiB", same);
    p1[12345] = 0xC0FFEEULL;
    check(p2[12345] == 0xC0FFEEULL, "a write through one mapping is seen through the other", (int64_t)p2[12345]);

    /* Rights */
    int64_t ro = h_dup(h, NX_RIGHT_READ | NX_RIGHT_MAP);
    check(ro > 0 && ro != h, "handle.dup(READ|MAP) gives a new handle", ro);
    r = shm_map(ro, NX_SHM_PROT_READ | NX_SHM_PROT_WRITE, 0);
    check(r == -EACCES, "read-only duplicate cannot map writable", r);
    int64_t a3 = shm_map(ro, NX_SHM_PROT_READ, 0);
    check(in_shared_region(a3), "read-only duplicate maps readable", a3);
    r = h_dup(ro, NX_RIGHTS_SAME);
    check(r == -EACCES, "handle without DUPLICATE cannot be duplicated", r);
    r = h_dup(h, 0x40);
    check(r == -EINVAL, "handle.dup with an unknown right is -EINVAL", r);
    r = h_dup(h, NX_RIGHT_READ | 0x20);
    check(r == -EINVAL, "handle.dup with right 0x20 is -EINVAL", r);

    /* A pointer into the mapping works as a syscall argument */
    const char *path = "/apps/shmtest";
    char *dst = (char *)a1;
    for (int i = 0; ; i++) { dst[i] = path[i]; if (!path[i]) break; }
    int64_t fd = sc3(SYS_OPEN, a1, VFS_O_RDONLY, 0);
    check(fd >= 0, "open() accepts a path stored in shared memory", fd);
    if (fd >= 0) sc1(SYS_CLOSE, fd);

    /* Mapping outlives its handle; stale handles */
    check(h_close(h) == 0, "handle.close while mapped", 0);
    check(h_close(h) == -EBADF, "second handle.close of the same handle is -EBADF", h_close(h));
    check(p2[12345] == 0xC0FFEEULL, "mapping still valid after its handle is closed", (int64_t)p2[12345]);
    r = shm_map(h, NX_SHM_PROT_READ, 0);
    check(r == -EBADF, "shm.map through a closed handle is -EBADF", r);

    r = shm_unmap(a1 + (int64_t)PAGE);
    check(r == -EINVAL, "shm.unmap of a non-base address is -EINVAL", r);
    check(shm_unmap(a1) == 0, "shm.unmap first mapping", 0);
    check(shm_unmap(a1) == -EINVAL, "second shm.unmap of the same address is -EINVAL", shm_unmap(a1));
    check(shm_unmap(a2) == 0 && shm_unmap(a3) == 0, "shm.unmap the other two mappings", 0);
    check(h_close(ro) == 0, "close the read-only duplicate", 0);
    check_memory_back("all 4 MiB returned after unmap and close (bytes missing)", baseline);

    /* Handle table: 256 handles, then -EMFILE; reused slots reject old values */
    int64_t base = shm_create(PAGE, 0);
    int64_t got = 1, last = 0, first_dup = 0;
    while (got < 300) {
        last = h_dup(base, NX_RIGHTS_SAME);
        if (last < 0) break;
        if (!first_dup) first_dup = last;
        got++;
    }
    check(got == NX_HANDLE_MAX && last == -EMFILE, "256 handles, then -EMFILE (handles held)", got);
    check(h_close(first_dup) == 0, "close one handle", 0);
    int64_t reused = h_dup(base, NX_RIGHTS_SAME);
    check(reused > 0 && reused != first_dup, "reused slot gets a new handle value", reused);
    r = h_close(first_dup);
    check(r == -EBADF, "old value of a reused slot is -EBADF", r);
    /* Close every handle we hold: each was returned once, so try the range */
    int closed = 0;
    for (int64_t gen = 1; gen < 4; gen++) {
        for (int64_t idx = 0; idx < NX_HANDLE_MAX; idx++) {
            if (h_close((gen << 8) | idx) == 0) closed++;
        }
    }
    check(closed == NX_HANDLE_MAX, "all 256 handles closed", closed);

    /* Mapping limit: 128, then -ENOMEM */
    int64_t small = shm_create(PAGE, 0);
    int64_t maps[NX_SHM_MAP_MAX + 1];
    int nmaps = 0;
    int64_t mr = 0;
    while (nmaps <= NX_SHM_MAP_MAX) {
        mr = shm_map(small, NX_SHM_PROT_READ, 0);
        if (mr < 0) break;
        maps[nmaps++] = mr;
    }
    check(nmaps == NX_SHM_MAP_MAX && mr == -ENOMEM, "128 mappings, then -ENOMEM (mappings held)", nmaps);
    for (int i = 0; i < nmaps; i++) shm_unmap(maps[i]);
    h_close(small);

    /* Quota: 4 x 64 MiB = 256 MiB, then -ENOMEM. free_memory() is
     * system-wide and the desktop keeps running (and allocating) while
     * 256 MiB is zeroed, so this check measures its own window: run once
     * unmeasured, let the system settle, then measure a second run. */
    int64_t q[5];
    int nq = 0;
    for (int round = 0; round < 2; round++) {
        uint64_t before = 0;
        if (round == 1) {
            sc1(SYS_SLEEP, 1000);
            before = free_memory();
        }
        nq = 0;
        for (int i = 0; i < 5; i++) {
            q[i] = shm_create(NX_SHM_SIZE_MAX, 0);
            if (q[i] > 0) nq++;
        }
        check(nq == 4 && q[4] == -ENOMEM, "256 MiB quota: 4 x 64 MiB, then -ENOMEM (objects created)", nq);
        for (int i = 0; i < 5; i++) if (q[i] > 0) h_close(q[i]);
        if (round == 1) check_memory_back("quota objects returned (bytes missing)", before);
    }

    /* A child that leaks 35 MiB of shm and exits. Spawning itself costs
     * memory the kernel keeps (file and heap caches), so an idle child is
     * spawned first and the baseline is taken after it. */
    const char *iargv[3] = { "shmtest", "idle", 0 };
    int status = -1;
    int64_t pid = sc3(SYS_SPAWN, (int64_t)"/apps/shmtest", (int64_t)iargv, SPAWN_WAITABLE);
    if (pid > 0) sc2(SYS_WAIT, pid, (int64_t)&status);
    check(pid > 0 && status == 0, "idle child (spawn warm-up) exited 0", status);
    uint64_t spawn_baseline = free_memory();

    const char *cargv[3] = { "shmtest", "child", 0 };
    status = -1;
    pid = sc3(SYS_SPAWN, (int64_t)"/apps/shmtest", (int64_t)cargv, SPAWN_WAITABLE);
    int64_t waited = (pid > 0) ? sc2(SYS_WAIT, pid, (int64_t)&status) : pid;
    check(pid > 0 && waited == pid && status == 0, "child created, mapped and leaked shm, exited 0", status);
    check_memory_back("child's leaked shm returned after it exited (bytes missing)", spawn_baseline);

    out_str(g_failed ? "[SHMTEST] RESULT: FAIL\n" : "[SHMTEST] RESULT: PASS\n");
    flush();
    finish(g_failed ? 1 : 0);
}
