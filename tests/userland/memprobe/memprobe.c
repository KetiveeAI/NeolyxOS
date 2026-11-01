/*
 * NeolyxOS memprobe - one memory snapshot for the regression runs
 *
 * Prints the kernel's memory counters (mem.stats, syscall 156, ABI 1.3)
 * as one machine-readable line, so scripts/regression.sh can compare free
 * memory at boot, after apps were opened and closed, and after memory
 * pressure:
 *
 *   [MEMPROBE] free_kb=... total_kb=... reserve_kb=... user_kb=...
 *              shm_kb=... kheap_kb=... refusals=... level=...
 *   [MEMPROBE] RESULT: PASS
 *
 * memprobe itself is small (one page of code, no heap), and its own pages
 * are reported as self_kb so they can be subtracted.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../../../kernel/include/core/nx_abi.h"
#include "../../../kernel/include/core/nx_mem.h"

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_ABI_QUERY    0
#define SYS_EXIT         1
#define SYS_DEBUG_PRINT  127
#define SYS_MEM_STATS    156

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

/* ============ Output (one bounded line at a time) ============ */

static char g_line[256];
static int g_len;

static void out_str(const char *s) {
    while (s && *s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_udec(uint64_t u) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 0) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    out_str(&buf[i]);
}

static void out_sdec(int64_t v) {
    if (v < 0) { out_str("-"); out_udec((uint64_t)(-(v + 1)) + 1); }
    else out_udec((uint64_t)v);
}

static void flush(void) {
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

static void kv(const char *key, uint64_t bytes_or_count, int as_kb) {
    out_str(" ");
    out_str(key);
    out_str("=");
    out_udec(as_kb ? bytes_or_count / 1024u : bytes_or_count);
}

static void finish(int code) {
    sc1(SYS_EXIT, code);
    for (;;) { __asm__ volatile("pause"); }
}

static const char *level_name(uint32_t level) {
    switch (level) {
    case NX_MEM_NORMAL:   return "normal";
    case NX_MEM_WARN:     return "warn";
    case NX_MEM_CRITICAL: return "critical";
    default:              return "unknown";
    }
}

void _start(void) {
    nx_abi_info_t abi;
    for (unsigned i = 0; i < sizeof(abi); i++) ((volatile char *)&abi)[i] = 0;

    int64_t rc = sc1(SYS_ABI_QUERY, (int64_t)&abi);
    if (rc != 0 || !(abi.flags & NX_ABI_FLAG_MEMCLASS)) {
        out_str("[MEMPROBE] mem.stats not available (abi.query rc ");
        out_sdec(rc);
        out_str(", flags ");
        out_udec(abi.flags);
        out_str(")\n");
        flush();
        out_str("[MEMPROBE] RESULT: FAIL\n");
        flush();
        finish(1);
    }

    nx_mem_stats_t st;
    for (unsigned i = 0; i < sizeof(st); i++) ((volatile char *)&st)[i] = 0;
    rc = sc2(SYS_MEM_STATS, (int64_t)&st, (int64_t)sizeof(st));
    if (rc < 0 || st.size < NX_MEM_STATS_V1_SIZE || st.total_bytes == 0 ||
        st.free_bytes > st.total_bytes) {
        out_str("[MEMPROBE] mem.stats failed or inconsistent (rc ");
        out_sdec(rc);
        out_str(", size ");
        out_udec(st.size);
        out_str(")\n");
        flush();
        out_str("[MEMPROBE] RESULT: FAIL\n");
        flush();
        finish(1);
    }

    out_str("[MEMPROBE]");
    kv("free_kb", st.free_bytes, 1);
    kv("total_kb", st.total_bytes, 1);
    kv("reserve_kb", st.reserve_bytes, 1);
    kv("user_kb", st.user_bytes, 1);
    kv("shm_kb", st.shm_bytes, 1);
    kv("kheap_kb", st.kheap_bytes, 1);
    kv("self_kb", st.self_bytes, 1);
    kv("refusals", st.refusals, 0);
    out_str(" level=");
    out_str(level_name(st.level));
    out_str("\n");
    flush();

    out_str("[MEMPROBE] RESULT: PASS\n");
    flush();
    finish(0);
}
