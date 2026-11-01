/*
 * NeolyxOS memstress - K0b memory reserve test
 *
 * Fills physical memory from ordinary (NORMAL class) processes and checks
 * that the kernel refuses them before free memory drops into the reserve,
 * while the desktop keeps running.
 *
 *   /apps/memstress          parent: spawns up to MS_CHILDREN copies of
 *                            itself with argv[1] = "child", waits for all,
 *                            prints a summary.
 *   /apps/memstress child    grows its heap 1 MiB at a time (writing every
 *                            page) until brk is refused or the 64 MiB heap
 *                            limit is hit, holds the memory for MS_HOLD_MS
 *                            so the desktop can be checked under pressure,
 *                            then exits.
 *
 * The kernel logs each refusal with the free memory and the reserve
 * ("[MEM] user mapping refused: ..."), which is what the test checks.
 *
 * K1: the parent also listens on a memory pressure handle. It must see
 * WARN and CRITICAL while the children fill memory, NORMAL again after
 * they exit, and mem.stats must count the refusals.
 * Children exit with 0 when stopped by a refusal, 1 when stopped by the
 * heap limit, 2 on a memory content error.
 *
 * K2: before the children start, the parent grows its own heap by
 * MS_TRIM_MB, shrinks it back with brk and checks with mem.stats that the
 * pages went back to the system (its own pages drop by exactly that much,
 * free memory rises), and that growing again gives zeroed pages.
 *
 * K2 part 2: after the first run, a second run starts "bg" children that
 * move themselves to BACKGROUND and fill memory, then one "fg" child.
 * Background apps must be refused at 1.5R free and the foreground app
 * must still get memory down to R. These children exit with
 * MS_FREE_CODE + the free MB they saw when refused (1 heap limit, 2 bad
 * memory, 3 could not change class).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "../../../kernel/include/core/nx_ipc.h"
#include "../../../kernel/include/core/nx_mem.h"

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_EXIT        1
#define SYS_SLEEP       2
#define SYS_TIME        3
#define SYS_GETPID      5
#define SYS_WAIT        9
#define SYS_BRK         10
#define SYS_SPAWN       13
#define SYS_DEBUG_PRINT 127
#define SYS_OBJ_WAIT    147
#define SYS_HANDLE_CLOSE 140
#define SYS_MEM_PRESSURE_OPEN 154
#define SYS_MEM_PRESSURE_READ 155
#define SYS_MEM_STATS   156

#define SPAWN_WAITABLE  0x1

#define MS_CHILDREN     40              /* 40 x 64 MiB covers a 2 GiB guest */
#define MS_STEP         (1024 * 1024)   /* heap growth per brk call */
#define MS_HEAP_LIMIT   (64ULL * 1024 * 1024)
#define MS_HOLD_MS      6000
#define MS_PAGE         4096
#define MS_TRIM_MB      32              /* K2 brk shrink check */
#define MS_CLASS_HOLD_MS 12000          /* K2 class run: hold while the rest fill */
#define MS_CLASS_GAP_MS 250             /* K2 class run: between spawns */
#define MS_FREE_CODE    16              /* K2 class run: exit code base */
#define MS_SLACK_MB     2               /* K2 class run: refusal level tolerance */
#define SYS_PROC_SET_CLASS 153

static inline int64_t sc0(int64_t n) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory");
    return r;
}

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

/* ============ Output ============ */

static char g_line[160];
static int g_len;

static void out_str(const char *s) {
    while (s && *s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_dec(int64_t v) {
    char buf[21];
    int i = 20;
    uint64_t u = (v < 0) ? (uint64_t)(-v) : (uint64_t)v;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 0) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (v < 0) out_str("-");
    out_str(&buf[i]);
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

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ============ Child: take memory until refused ============ */

static void run_child(void) {
    int64_t pid = sc0(SYS_GETPID);
    int64_t base = sc1(SYS_BRK, 0);
    if (base <= 0) {
        out_str("[MEMSTRESS] child PID "); out_dec(pid);
        out_str(": FAIL: no heap ("); out_dec(base); out_str(")\n");
        flush();
        finish(2);
    }

    uint64_t got = 0;
    int refused = 0;
    while (got + MS_STEP <= MS_HEAP_LIMIT) {
        int64_t want = base + (int64_t)(got + MS_STEP);
        int64_t r = sc1(SYS_BRK, want);
        if (r < want) { refused = 1; break; }
        /* Touch every page so the memory is really in use, tagged by pid */
        volatile uint64_t *p = (volatile uint64_t *)(base + (int64_t)got);
        for (uint64_t off = 0; off < MS_STEP; off += MS_PAGE) {
            p[off / 8] = ((uint64_t)pid << 32) | (got + off);
        }
        got += MS_STEP;
    }

    /* Check what we hold still reads back */
    int bad = 0;
    for (uint64_t off = 0; off < got; off += MS_PAGE) {
        volatile uint64_t *p = (volatile uint64_t *)(base + (int64_t)off);
        if (*p != (((uint64_t)pid << 32) | off)) { bad = 1; break; }
    }

    out_str("[MEMSTRESS] child PID "); out_dec(pid);
    out_str(": holding "); out_dec((int64_t)(got / MS_STEP));
    out_str(" MB, stopped by ");
    out_str(refused ? "refusal" : "heap limit");
    out_str(bad ? ", FAIL: content mismatch\n" : "\n");
    flush();

    sc1(SYS_SLEEP, MS_HOLD_MS);
    finish(bad ? 2 : (refused ? 0 : 1));
}

/* ============ Class children (K2 part 2) ============ */

/* Fill like run_child, but report the free memory seen at the refusal
 * in the exit code. background: move to BACKGROUND first. */
static void run_class_child(int background) {
    int64_t pid = sc0(SYS_GETPID);
    if (background && sc2(SYS_PROC_SET_CLASS, pid, NX_CLASS_BACKGROUND) != 0) {
        out_str("[MEMSTRESS] class child PID "); out_dec(pid);
        out_str(": FAIL: cannot move to BACKGROUND\n");
        flush();
        finish(3);
    }
    int64_t base = sc1(SYS_BRK, 0);
    if (base <= 0) finish(2);

    uint64_t got = 0;
    int refused = 0;
    uint64_t free_mb = 0;
    while (got + MS_STEP <= MS_HEAP_LIMIT) {
        int64_t want = base + (int64_t)(got + MS_STEP);
        if (sc1(SYS_BRK, want) < want) {
            nx_mem_stats_t st;
            if (sc2(SYS_MEM_STATS, (int64_t)&st, sizeof(st)) == 0) free_mb = st.free_bytes >> 20;
            refused = 1;
            break;
        }
        volatile uint64_t *p = (volatile uint64_t *)(base + (int64_t)got);
        for (uint64_t off = 0; off < MS_STEP; off += MS_PAGE) p[off / 8] = ((uint64_t)pid << 32) | (got + off);
        got += MS_STEP;
    }
    int bad = 0;
    for (uint64_t off = 0; off < got; off += MS_PAGE) {
        volatile uint64_t *p = (volatile uint64_t *)(base + (int64_t)off);
        if (*p != (((uint64_t)pid << 32) | off)) { bad = 1; break; }
    }
    out_str("[MEMSTRESS] "); out_str(background ? "background" : "foreground");
    out_str(" child PID "); out_dec(pid);
    out_str(": holding "); out_dec((int64_t)(got / MS_STEP)); out_str(" MB");
    if (refused) { out_str(", refused at "); out_dec((int64_t)free_mb); out_str(" MB free"); }
    else out_str(", heap limit");
    out_str(bad ? ", FAIL: content mismatch\n" : "\n");
    flush();
    sc1(SYS_SLEEP, MS_CLASS_HOLD_MS);
    if (bad) finish(2);
    if (!refused) finish(1);
    finish(MS_FREE_CODE + (int)(free_mb > 200 ? 200 : free_mb));
}

/* ============ Parent: pressure listener (K1) ============ */

static int64_t g_ph = -1;           /* pressure handle, or < 0 */
static int g_seen[3];               /* levels seen */
static int g_last_level = -1;

static const char *level_name(int l) {
    return l == 0 ? "NORMAL" : l == 1 ? "WARN" : l == 2 ? "CRITICAL" : "?";
}

/* Wait up to ms for a level change; record and print it. Returns the
 * level read, or -1 when nothing changed. */
static int poll_pressure(uint64_t ms) {
    if (g_ph < 0) return -1;
    nx_wait_item_t it = { (nx_handle_t)g_ph, NX_SIGNAL_READABLE, 0, 0 };
    int64_t r = sc3(SYS_OBJ_WAIT, (int64_t)&it, 1, (int64_t)ms);
    if (r <= 0) return -1;
    int64_t level = sc1(SYS_MEM_PRESSURE_READ, g_ph);
    if (level < 0 || level > 2) return -1;
    g_seen[level] = 1;
    if (level != g_last_level) {
        nx_mem_stats_t st;
        sc2(SYS_MEM_STATS, (int64_t)&st, sizeof(st));
        out_str("[MEMSTRESS] pressure "); out_str(level_name((int)level));
        out_str(": "); out_dec((int64_t)(st.free_bytes >> 20));
        out_str(" MB free, "); out_dec((int64_t)st.refusals); out_str(" refusals so far\n");
        flush();
        g_last_level = (int)level;
    }
    return (int)level;
}

/* ============ Parent: brk shrink gives pages back (K2) ============ */

/* Returns 1 on PASS, 0 on FAIL, -1 when mem.stats is missing (skipped). */
static int check_trim(void) {
    nx_mem_stats_t a, b, c;
    if (sc2(SYS_MEM_STATS, (int64_t)&a, sizeof(a)) != 0) return -1;
    int64_t base = sc1(SYS_BRK, 0);
    uint64_t size = (uint64_t)MS_TRIM_MB << 20;
    if (base <= 0 || sc1(SYS_BRK, base + (int64_t)size) != base + (int64_t)size) {
        out_str("[MEMSTRESS] trim: FAIL: cannot grow the heap by ");
        out_dec(MS_TRIM_MB); out_str(" MB\n");
        flush();
        return 0;
    }
    volatile uint64_t *p = (volatile uint64_t *)base;
    for (uint64_t off = 0; off < size; off += MS_PAGE) p[off / 8] = 0xA5A5A5A5A5A5A5A5ULL ^ off;
    sc2(SYS_MEM_STATS, (int64_t)&b, sizeof(b));

    int64_t r = sc1(SYS_BRK, base);
    sc2(SYS_MEM_STATS, (int64_t)&c, sizeof(c));
    uint64_t self_drop = (b.self_bytes > c.self_bytes) ? b.self_bytes - c.self_bytes : 0;
    uint64_t free_rise = (c.free_bytes > b.free_bytes) ? c.free_bytes - b.free_bytes : 0;

    /* Grow again: the pages must be new and zeroed, not the old ones */
    int stale = 0;
    uint64_t again = 4ULL << 20;
    if (sc1(SYS_BRK, base + (int64_t)again) != base + (int64_t)again) {
        stale = -1;
    } else {
        for (uint64_t off = 0; off < again; off += MS_PAGE) {
            if (p[off / 8] != 0) { stale = 1; break; }
        }
        sc1(SYS_BRK, base);
    }

    /* Own pages are exact; free memory may move a little as other
     * processes run, so allow 1 MB of noise */
    int ok = (r == base) && self_drop == size && free_rise + (1ULL << 20) >= size && stale == 0;
    out_str("[MEMSTRESS] trim: grew "); out_dec(MS_TRIM_MB);
    out_str(" MB then shrank: own pages -"); out_dec((int64_t)(self_drop >> 20));
    out_str(" MB, free memory +"); out_dec((int64_t)(free_rise >> 20));
    out_str(" MB, regrown pages ");
    out_str(stale == 0 ? "zeroed" : stale > 0 ? "NOT zeroed" : "refused");
    out_str(ok ? " (PASS)\n" : " (FAIL)\n");
    flush();
    return ok;
}

/* ============ Parent: background apps hit the limit first (K2 part 2) ============ */

static int within(uint64_t mb, uint64_t floor_mb) {
    return mb + MS_SLACK_MB >= floor_mb && mb <= floor_mb + MS_SLACK_MB;
}

/* Returns 1 PASS, 0 FAIL, -1 skipped (no mem.stats). */
static int check_classes(void) {
    nx_mem_stats_t st;
    if (sc2(SYS_MEM_STATS, (int64_t)&st, sizeof(st)) != 0) return -1;
    uint64_t r_mb = st.reserve_bytes >> 20;
    uint64_t bg_floor_mb = (st.reserve_bytes + st.reserve_bytes / 2) >> 20;
    int nbg = (int)((st.free_bytes >> 20) / (MS_HEAP_LIMIT >> 20)) + 3;
    if (nbg > MS_CHILDREN - 1) nbg = MS_CHILDREN - 1;
    out_str("[MEMSTRESS] class run: "); out_dec(nbg);
    out_str(" background children, then 1 foreground; floors: background ");
    out_dec((int64_t)bg_floor_mb); out_str(" MB, foreground "); out_dec((int64_t)r_mb);
    out_str(" MB\n");
    flush();

    static const char *bg_argv[] = { "/apps/memstress", "bg", 0 };
    static const char *fg_argv[] = { "/apps/memstress", "fg", 0 };
    int32_t kids[MS_CHILDREN];
    int n = 0, failed = 0;
    for (int i = 0; i <= nbg; i++) {
        int fg = (i == nbg);
        int64_t c = sc3(SYS_SPAWN, (int64_t)"/apps/memstress",
                        (int64_t)(fg ? fg_argv : bg_argv), SPAWN_WAITABLE);
        if (c <= 0) {
            out_str("[MEMSTRESS] class run: spawn "); out_dec(i);
            out_str(" refused ("); out_dec(c); out_str(") (FAIL)\n");
            flush();
            failed++;
            break;
        }
        kids[n++] = (int32_t)c;
        sc1(SYS_SLEEP, MS_CLASS_GAP_MS);
    }

    int bg_refused = 0, bg_wrong = 0, fg_ok = 0;
    uint64_t bg_min = ~0ULL, bg_max = 0, fg_mb = 0;
    for (int i = 0; i < n; i++) {
        int status = -1;
        int is_fg = (i == nbg);
        if (sc2(SYS_WAIT, kids[i], (int64_t)&status) < 0) { failed++; continue; }
        if (status >= MS_FREE_CODE) {
            uint64_t mb = (uint64_t)(status - MS_FREE_CODE);
            if (is_fg) {
                fg_mb = mb;
                fg_ok = within(mb, r_mb);
            } else {
                bg_refused++;
                if (mb < bg_min) bg_min = mb;
                if (mb > bg_max) bg_max = mb;
                if (!within(mb, bg_floor_mb)) bg_wrong++;
            }
        } else if (status != 1) {
            failed++;           /* 2 bad memory, 3 class change refused */
        }
    }

    int ok = failed == 0 && bg_refused > 0 && bg_wrong == 0 && fg_ok;
    out_str("[MEMSTRESS] class run: "); out_dec(bg_refused);
    out_str(" background refused at ");
    if (bg_refused) { out_dec((int64_t)bg_min); out_str("-"); out_dec((int64_t)bg_max); }
    else out_str("-");
    out_str(" MB free (floor "); out_dec((int64_t)bg_floor_mb);
    out_str("), foreground refused at "); out_dec((int64_t)fg_mb);
    out_str(" MB free (floor "); out_dec((int64_t)r_mb); out_str(")");
    out_str(ok ? " (PASS)\n" : " (FAIL)\n");
    flush();
    return ok;
}

/* ============ Parent: spawn children and wait ============ */

void _start(long argc, char **argv) {
    if (argc > 1 && argv && str_eq(argv[1], "child")) run_child();
    if (argc > 1 && argv && str_eq(argv[1], "bg")) run_class_child(1);
    if (argc > 1 && argv && str_eq(argv[1], "fg")) run_class_child(0);

    int64_t pid = sc0(SYS_GETPID);
    out_str("[MEMSTRESS] PID "); out_dec(pid);
    out_str(" starting up to "); out_dec(MS_CHILDREN);
    out_str(" children of 64 MB each\n");
    flush();

    nx_mem_stats_t st0;
    int have_stats = (sc2(SYS_MEM_STATS, (int64_t)&st0, sizeof(st0)) == 0);
    g_ph = sc1(SYS_MEM_PRESSURE_OPEN, 0);
    if (g_ph < 0 || !have_stats) {
        out_str("[MEMSTRESS] no memory pressure API (ABI < 1.3): checking refusals only\n");
        flush();
        g_ph = -1;
    } else {
        g_last_level = (int)sc1(SYS_MEM_PRESSURE_READ, g_ph);
        out_str("[MEMSTRESS] start: "); out_dec((int64_t)(st0.free_bytes >> 20));
        out_str(" MB free, reserve "); out_dec((int64_t)(st0.reserve_bytes >> 20));
        out_str(" MB, level "); out_str(level_name(g_last_level)); out_str("\n");
        flush();
    }

    int trim_ok = check_trim();

    static const char *child_argv[] = { "/apps/memstress", "child", 0 };
    int32_t kids[MS_CHILDREN];
    int nkids = 0, spawn_refused = 0;
    for (int i = 0; i < MS_CHILDREN; i++) {
        int64_t c = sc3(SYS_SPAWN, (int64_t)"/apps/memstress",
                        (int64_t)child_argv, SPAWN_WAITABLE);
        if (c <= 0) {
            out_str("[MEMSTRESS] spawn "); out_dec(i);
            out_str(" refused ("); out_dec(c); out_str(")\n");
            flush();
            spawn_refused = 1;
            break;
        }
        kids[nkids++] = (int32_t)c;
        /* Let the child fill up before starting the next, so the order of
         * refusals is easy to read in the log; watch the pressure level */
        if (g_ph >= 0) {
            int64_t until = sc0(SYS_TIME) + 150;
            while (sc0(SYS_TIME) < until) poll_pressure(50);
        } else {
            sc1(SYS_SLEEP, 150);
        }
    }

    int refused = 0, capped = 0, failed = 0;
    for (int i = 0; i < nkids; i++) {
        int status = -1;
        int64_t w = sc2(SYS_WAIT, kids[i], (int64_t)&status);
        if (w < 0) { failed++; continue; }
        if (status == 0) refused++;
        else if (status == 1) capped++;
        else failed++;
    }

    /* Memory is back: the level must return to NORMAL */
    int pressure_ok = 1;
    if (g_ph >= 0) {
        for (int i = 0; i < 30 && g_last_level != 0; i++) poll_pressure(100);
        nx_mem_stats_t st1;
        sc2(SYS_MEM_STATS, (int64_t)&st1, sizeof(st1));
        uint64_t new_refusals = st1.refusals - st0.refusals;
        pressure_ok = g_seen[1] && g_seen[2] && g_last_level == 0 && new_refusals > 0;
        out_str("[MEMSTRESS] pressure seen: WARN "); out_str(g_seen[1] ? "yes" : "no");
        out_str(", CRITICAL "); out_str(g_seen[2] ? "yes" : "no");
        out_str(", back to "); out_str(level_name(g_last_level));
        out_str("; mem.stats refusals +"); out_dec((int64_t)new_refusals);
        out_str(pressure_ok ? "\n" : " (FAIL)\n");
        flush();
        sc1(SYS_HANDLE_CLOSE, g_ph);
        if (!pressure_ok) failed++;
    }

    if (trim_ok == 0) failed++;

    /* K2 part 2 runs with memory back to normal */
    int class_ok = check_classes();
    if (class_ok == 0) failed++;

    out_str("[MEMSTRESS] done: "); out_dec(nkids);
    out_str(" children, "); out_dec(capped);
    out_str(" reached 64 MB, "); out_dec(refused);
    out_str(" refused, "); out_dec(failed);
    out_str(" failed");
    out_str(spawn_refused ? ", spawn refused\n" : "\n");
    flush();
    out_str((failed == 0 && (refused > 0 || spawn_refused)) ? "[MEMSTRESS] PASS\n"
            : (failed == 0 ? "[MEMSTRESS] memory never ran low (more RAM than children)\n"
                           : "[MEMSTRESS] FAIL\n"));
    flush();
    finish(failed ? 1 : 0);
}
