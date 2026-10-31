/*
 * NeolyxOS - NXProc: userspace process and system statistics client
 * (see nxproc.h for the contract)
 *
 * Layout of this file:
 *   1. Small freestanding helpers and log-once reporting.
 *   2. Backend: the only code that talks to the kernel. Two variants:
 *        - NeolyxOS build: real syscalls where they exist (SYS_INFO,
 *          SYS_GETPID, SYS_GETPPID), NXPROC_ERR_UNSUPPORTED elsewhere.
 *        - NX_HOST_PREVIEW build: a deterministic DEMO backend so the
 *          Task Manager can be designed on the host. It is compiled out
 *          of every NeolyxOS build. Define NXPROC_NO_DEMO as well to see
 *          the real-kernel code paths (and the "unavailable" UI) in the
 *          preview, where the host answers unknown syscalls with ENOSYS.
 *   3. Common code: CPU % from deltas between samples, privilege
 *      pre-checks, logging. Shared by both backends so the delta maths
 *      that real kernels will feed is exercised in the preview too.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxproc.h"
#include "nxsyscall.h"

#define NXPROC_SAMPLER_MAGIC 0x4E585052u   /* "NXPR" */

/* ============ 1. Helpers ============ */

static void np_copy(char *dst, uint32_t cap, const char *src) {
    if (!dst || cap == 0) return;
    uint32_t i = 0;
    if (src) {
        while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    }
    dst[i] = '\0';
}

static void np_zero(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void np_log(const char *a, const char *b) {
    char line[160];
    np_copy(line, sizeof(line), "[NXPROC] ");
    uint32_t n = 9;
    for (const char *s = a; s && *s && n + 2 < sizeof(line); s++) line[n++] = *s;
    for (const char *s = b; s && *s && n + 2 < sizeof(line); s++) line[n++] = *s;
    line[n++] = '\n';
    line[n] = '\0';
    nx_debug_print(line);
}

/* Unsigned decimal into buf (cap >= 2). */
static void np_u64(char *buf, uint32_t cap, uint64_t v) {
    char tmp[24];
    int n = 0;
    do { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && n < 23);
    uint32_t i = 0;
    while (n > 0 && i + 1 < cap) buf[i++] = tmp[--n];
    buf[i] = '\0';
}

/* Operations that can be unsupported; each is logged only once. */
enum { OP_LIST = 0, OP_CPU, OP_END, OP_PRIO, OP_STARTUP, OP_SYSINFO, OP_COUNT };
static uint8_t g_logged_unsupported[OP_COUNT];

static nxproc_status_t np_unsupported(int op, const char *what) {
    if (op >= 0 && op < OP_COUNT && !g_logged_unsupported[op]) {
        g_logged_unsupported[op] = 1;
        np_log("unsupported by this kernel (no userspace syscall): ", what);
    }
    return NXPROC_ERR_UNSUPPORTED;
}

/* Last known flags per pid, for the client-side protection pre-check. */
typedef struct { uint32_t pid; uint32_t flags; } np_flag_t;
static np_flag_t g_known[NXPROC_MAX_PROCS];
static uint32_t  g_known_count;

/* ============ 2. Backend ============ */

/* Raw system counters a backend reports; the common code turns the
 * cumulative busy counters into percentages. */
typedef struct {
    uint32_t valid;                     /* NXPROC_SYS_* (CPU bits mean counters valid) */
    uint64_t now_ms;
    uint32_t cpus;
    uint64_t busy_ms;                   /* cumulative busy core-ms, all cores */
    uint64_t core_busy_ms[NXPROC_MAX_CPUS];
    uint64_t mem_total, mem_free;
    uint64_t uptime_ms;
    uint32_t procs, threads;
} np_raw_sys_t;

#if defined(NX_HOST_PREVIEW) && !defined(NXPROC_NO_DEMO)
/* ================================================================== *
 *  DEMO BACKEND - host preview only. Nothing below exists on NeolyxOS.
 *  Deterministic synthetic processes whose CPU time grows by a smooth
 *  pseudo-random load, so CPU % deltas, graphs and sorting look alive.
 * ================================================================== */

typedef struct {
    uint32_t    pid, ppid;
    const char *name;
    uint32_t    flags;
    uint16_t    base_permille;   /* typical load, per mille of one core (may exceed 1000) */
    uint16_t    swing_permille;  /* noise amplitude */
    uint32_t    mem_mb;
    uint32_t    threads;
    uint8_t     prio;
    uint32_t    start_s;         /* seconds after boot */
} demo_def_t;

#define DK (NXPROC_F_KERNEL | NXPROC_F_SYSTEM)
#define DS (NXPROC_F_USER | NXPROC_F_SYSTEM)
#define DU (NXPROC_F_USER)
static const demo_def_t g_demo_defs[] = {
    {  1,  0, "kernel_task",   DK,  30,  20, 412, 14,   0,   0 },
    {  2,  1, "nxinit",        DS,   1,   1,   6,  1,  64,   0 },
    {  3,  1, "nxtask.kdrv",   DK,   4,   3,   2,  1,  32,   0 },
    {  4,  1, "nxfs.kdrv",     DK,  12,  25,  38,  4,  32,   0 },
    {  5,  1, "nxnet.kdrv",    DK,   6,   8,  11,  2,  32,   0 },
    {  6,  1, "nxgfx.kdrv",    DK,  40,  30,  64,  3,  32,   0 },
    {  7,  1, "nxaudio.kdrv",  DK,   8,   5,   9,  2,  32,   0 },
    { 10,  2, "nxlogind",      DS,   1,   1,  14,  2,  96,   2 },
    { 11,  2, "nxconfigd",     DS,   2,   2,  12,  2,  96,   2 },
    { 12,  2, "nxnetd",        DS,   5,   6,  21,  3,  96,   3 },
    { 13,  2, "nxtimed",       DS,   1,   1,   4,  1, 128,   3 },
    { 14,  2, "desktop",       DS, 380, 260, 186,  6,  64,   4 },
    { 20, 14, "nxindexd",      DU, 120, 520,  96,  4, 192,   9 },
    { 21, 14, "nxthumbd",      DU,  60, 900,  74,  3, 192,  12 },
    { 22, 14, "Shell",      DU,  18,  15,  58,  3, 128,  40 },
    { 23, 22, "nxsh",          DU,   2,   3,   7,  1, 128,  41 },
    { 24, 14, "Files",         DU,  25,  30, 132,  5, 128,  65 },
    { 25, 14, "Notes",         DU,   6,   8,  71,  3, 128,  88 },
    { 26, 14, "Music",         DU,  45,  20, 154,  7, 128, 120 },
    { 27, 14, "Mail",          DU,  12,  25, 168,  6, 128, 140 },
    { 28, 14, "Photos",        DU, 260, 700, 389,  9, 128, 210 },
    { 29, 14, "Calendar",      DU,   3,   4,  48,  2, 128, 260 },
    { 30, 14, "nxupdate",      DU,   1,  40,  33,  2, 160, 300 },
    { 31, 22, "build-nxfs",    DU,2100,2600, 512,  8, 128, 512 },
};
#undef DK
#undef DS
#undef DU
#define DEMO_N ((uint32_t)(sizeof(g_demo_defs) / sizeof(g_demo_defs[0])))
#define DEMO_CPUS 8u
#define DEMO_SELF_PID 14u
#define DEMO_BOOT_S 7200u                     /* demo machine has been up 2 h */
#define DEMO_MEM_TOTAL (16ull << 30)

typedef struct {
    uint8_t  alive;
    uint8_t  prio;
    uint8_t  terminating;
    uint32_t ppid;
    uint64_t term_at_ms;
    double   cpu_ms;                          /* cumulative CPU time */
} demo_live_t;

static demo_live_t g_demo[DEMO_N];
static double   g_demo_core_ms[DEMO_CPUS];
static uint64_t g_demo_offset_ms;             /* advanced by nxproc_demo_advance */
static uint64_t g_demo_last_ms;
static int      g_demo_ready;

/* Exposed to the preview Task Manager only (declared there under
 * NX_HOST_PREVIEW): fast-forward the demo clock to pre-fill graphs. */
void nxproc_demo_advance(uint32_t ms);
void nxproc_demo_advance(uint32_t ms) { g_demo_offset_ms += ms; }

static uint64_t demo_now(void) {
    return (uint64_t)DEMO_BOOT_S * 1000u + nx_gettime() + g_demo_offset_ms;
}

static uint32_t demo_hash(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

/* Smooth value noise in [0,1): period 2.5 s, smoothstep interpolation. */
static double demo_noise(uint32_t seed, uint64_t t_ms) {
    uint64_t k = t_ms / 2500u;
    double f = (double)(t_ms % 2500u) / 2500.0;
    double a = (double)(demo_hash(seed, (uint32_t)k) & 0xFFFF) / 65536.0;
    double b = (double)(demo_hash(seed, (uint32_t)k + 1u) & 0xFFFF) / 65536.0;
    f = f * f * (3.0 - 2.0 * f);
    return a + (b - a) * f;
}

/* Load of process i at t, in cores (at most one core per thread). */
static double demo_load(uint32_t i, uint64_t t_ms) {
    const demo_def_t *d = &g_demo_defs[i];
    double n = demo_noise(d->pid * 31u + 7u, t_ms);
    double v = ((double)d->base_permille + (double)d->swing_permille * (n * n * 1.6 - 0.35)) / 1000.0;
    double cap = (double)(d->threads < DEMO_CPUS ? d->threads : DEMO_CPUS);
    if (v < 0.0) v = 0.0;
    if (v > cap) v = cap;
    return v;
}

static void demo_init(void) {
    if (g_demo_ready) return;
    uint64_t now = demo_now();
    for (uint32_t i = 0; i < DEMO_N; i++) {
        const demo_def_t *d = &g_demo_defs[i];
        g_demo[i].alive = 1;
        g_demo[i].prio = d->prio;
        g_demo[i].ppid = d->ppid;
        uint64_t age = now - (uint64_t)d->start_s * 1000u;
        g_demo[i].cpu_ms = (double)age * ((double)d->base_permille / 1000.0) * 0.9;
    }
    for (uint32_t c = 0; c < DEMO_CPUS; c++) g_demo_core_ms[c] = (double)now * 0.08;
    g_demo_last_ms = now;
    g_demo_ready = 1;
}

/* Integrate loads from the last update to now in 100 ms steps (bounded). */
static void demo_advance(void) {
    demo_init();
    uint64_t now = demo_now();
    if (now <= g_demo_last_ms) return;
    uint64_t span = now - g_demo_last_ms;
    if (span > 600000u) g_demo_last_ms = now - 600000u;   /* cap the work */
    while (g_demo_last_ms < now) {
        uint64_t step = now - g_demo_last_ms;
        if (step > 100u) step = 100u;
        uint64_t t = g_demo_last_ms + step / 2u;
        double total = 0.0;
        for (uint32_t i = 0; i < DEMO_N; i++) {
            if (!g_demo[i].alive) continue;
            if (g_demo[i].terminating && t >= g_demo[i].term_at_ms) {
                g_demo[i].alive = 0;
                continue;
            }
            double l = demo_load(i, t);
            g_demo[i].cpu_ms += l * (double)step;
            total += l;
        }
        /* Spread total busy over the cores with uneven weights. */
        double w[DEMO_CPUS], ws = 0.0;
        for (uint32_t c = 0; c < DEMO_CPUS; c++) {
            w[c] = 0.25 + demo_noise(1000u + c, t) * (c < 2 ? 1.6 : 1.0);
            ws += w[c];
        }
        for (uint32_t c = 0; c < DEMO_CPUS; c++) {
            double share = total * w[c] / ws;
            if (share > 1.0) share = 1.0;
            g_demo_core_ms[c] += share * (double)step;
        }
        g_demo_last_ms += step;
    }
}

static int demo_index(uint32_t pid) {
    for (uint32_t i = 0; i < DEMO_N; i++)
        if (g_demo_defs[i].pid == pid && g_demo[i].alive) return (int)i;
    return -1;
}

static nxproc_status_t be_self(uint32_t *pid, uint32_t *ppid) {
    *pid = DEMO_SELF_PID;
    *ppid = 2u;
    return NXPROC_OK;
}

static nxproc_status_t be_list(nxproc_info_t *out, uint32_t cap, uint32_t *count,
                               uint32_t *kernel_count, uint64_t *now_ms) {
    demo_advance();
    uint32_t n = 0, total = 0;
    for (uint32_t i = 0; i < DEMO_N; i++) {
        if (!g_demo[i].alive) continue;
        total++;
        if (n >= cap) continue;
        const demo_def_t *d = &g_demo_defs[i];
        nxproc_info_t *p = &out[n++];
        np_zero(p, sizeof(*p));
        p->pid = d->pid;
        p->ppid = g_demo[i].ppid;
        np_copy(p->name, sizeof(p->name), d->name);
        p->state = (d->base_permille > 150) ? NXPROC_STATE_RUNNING
                 : (d->base_permille > 10) ? NXPROC_STATE_READY : NXPROC_STATE_SLEEPING;
        if (g_demo[i].terminating) p->state = NXPROC_STATE_ZOMBIE;
        p->priority = g_demo[i].prio;
        p->flags = d->flags;
        p->cpu_time_ms = (uint64_t)g_demo[i].cpu_ms;
        double wob = demo_noise(d->pid * 13u + 3u, g_demo_last_ms) * 0.06;
        p->resident_bytes = (uint64_t)((double)d->mem_mb * (0.97 + wob) * 1048576.0);
        p->threads = d->threads;
        p->start_ms = (uint64_t)d->start_s * 1000u;
        np_copy(p->owner, sizeof(p->owner), (d->flags & NXPROC_F_SYSTEM) ? "system" : "user");
    }
    *count = n;
    *kernel_count = total;
    *now_ms = g_demo_last_ms;
    return NXPROC_OK;
}

static nxproc_status_t be_sys(np_raw_sys_t *o) {
    demo_advance();
    o->valid = NXPROC_SYS_MEMORY | NXPROC_SYS_UPTIME | NXPROC_SYS_CPU |
               NXPROC_SYS_PER_CORE | NXPROC_SYS_PROCS;
    o->now_ms = g_demo_last_ms;
    o->cpus = DEMO_CPUS;
    double busy = 0.0;
    for (uint32_t c = 0; c < DEMO_CPUS; c++) {
        o->core_busy_ms[c] = (uint64_t)g_demo_core_ms[c];
        busy += g_demo_core_ms[c];
    }
    o->busy_ms = (uint64_t)busy;
    uint64_t used = 1700ull << 20;            /* kernel + caches */
    uint32_t procs = 0, threads = 0;
    for (uint32_t i = 0; i < DEMO_N; i++) {
        if (!g_demo[i].alive) continue;
        procs++;
        threads += g_demo_defs[i].threads;
        used += (uint64_t)g_demo_defs[i].mem_mb << 20;
    }
    used += (uint64_t)(demo_noise(4242u, g_demo_last_ms / 4u) * (double)(2600ull << 20));
    o->mem_total = DEMO_MEM_TOTAL;
    o->mem_free = DEMO_MEM_TOTAL - used;
    o->uptime_ms = g_demo_last_ms;
    o->procs = procs;
    o->threads = threads;
    return NXPROC_OK;
}

static nxproc_status_t be_end(uint32_t pid, nxproc_end_mode_t mode, uint32_t grace_ms) {
    demo_advance();
    int i = demo_index(pid);
    if (i < 0) return NXPROC_ERR_NOT_FOUND;
    if (g_demo_defs[i].flags & NXPROC_F_SYSTEM) return NXPROC_ERR_PROTECTED;
    if (mode == NXPROC_END_FORCE) {
        g_demo[i].alive = 0;
    } else if (!g_demo[i].terminating) {
        /* A well-behaved app quits after ~0.6 s; "build-nxfs" ignores the
         * request and the kernel forces it when the grace period ends. */
        uint32_t quit = (pid == 31u) ? grace_ms : 600u;
        g_demo[i].terminating = 1;
        g_demo[i].term_at_ms = g_demo_last_ms + quit;
    }
    /* Children are re-parented to nxinit, as the kernel would. */
    for (uint32_t k = 0; k < DEMO_N; k++)
        if (g_demo[k].alive && g_demo[k].ppid == pid && mode == NXPROC_END_FORCE) g_demo[k].ppid = 2u;
    return NXPROC_OK;
}

static nxproc_status_t be_prio(uint32_t pid, uint8_t prio) {
    int i = demo_index(pid);
    if (i < 0) return NXPROC_ERR_NOT_FOUND;
    if ((g_demo_defs[i].flags & NXPROC_F_SYSTEM) && prio != g_demo[i].prio) return NXPROC_ERR_PERMISSION;
    g_demo[i].prio = prio;
    return NXPROC_OK;
}

static nxproc_status_t be_startup(nxproc_startup_t *out, uint32_t cap, uint32_t *count) {
    static const struct { const char *n, *p; uint8_t en, imp; } items[] = {
        { "File Manager",    "/apps/files.sys",            1, 2 },
        { "System Settings", "/apps/settings.sys",         0, 1 },
        { "Font Manager",    "/apps/fontmanager.sys",      0, 1 },
        { "nxindexd",        "/System/Services/nxindexd",  1, 3 },
        { "nxupdate",        "/System/Services/nxupdate",  1, 1 },
    };
    uint32_t n = 0;
    for (uint32_t i = 0; i < sizeof(items) / sizeof(items[0]) && n < cap; i++, n++) {
        np_copy(out[n].name, sizeof(out[n].name), items[i].n);
        np_copy(out[n].path, sizeof(out[n].path), items[i].p);
        out[n].enabled = items[i].en;
        out[n].impact = items[i].imp;
    }
    *count = n;
    return NXPROC_OK;
}

static uint32_t be_caps(void) {
    return NXPROC_CAP_LIST | NXPROC_CAP_SYS_MEM | NXPROC_CAP_SYS_CPU | NXPROC_CAP_END |
           NXPROC_CAP_PRIORITY | NXPROC_CAP_STARTUP | NXPROC_CAP_DEMO;
}

#else  /* ======================= NeolyxOS backend ======================= */

/* Mirror of the kernel's sysinfo_t (kernel/include/core/syscall.h) for
 * SYS_INFO (50). Kept local: the kernel header clashes with nxsyscall.h.
 * The layout is part of the kernel ABI; the size check catches drift. */
typedef struct {
    char     os_name[32];
    char     os_version[32];
    char     arch[16];
    uint64_t uptime_ms;          /* PIT ticks; the PIT runs at 1000 Hz */
    uint64_t total_memory;
    uint64_t free_memory;
    uint32_t num_cpus;
    uint32_t num_processes;      /* kernel TODO: always 1, not used */
} np_k_sysinfo_t;
_Static_assert(sizeof(np_k_sysinfo_t) == 112, "sysinfo_t mirror out of sync with the kernel");

#define NP_SYS_GETPPID 6
#define NP_SYS_INFO    50
#define NP_ENOSYS      38

static nxproc_status_t be_self(uint32_t *pid, uint32_t *ppid) {
    int64_t p = syscall0(SYS_GETPID);
    int64_t pp = syscall0(NP_SYS_GETPPID);
    if (p == -NP_ENOSYS || pp == -NP_ENOSYS) return NXPROC_ERR_UNSUPPORTED;
    if (p < 0 || pp < 0) {
        np_log("getpid/getppid failed", "");
        return NXPROC_ERR_KERNEL;
    }
    *pid = (uint32_t)p;
    *ppid = (uint32_t)pp;
    return NXPROC_OK;
}

static nxproc_status_t be_list(nxproc_info_t *out, uint32_t cap, uint32_t *count,
                               uint32_t *kernel_count, uint64_t *now_ms) {
    (void)out; (void)cap; (void)now_ms;
    *count = 0;
    *kernel_count = 0;
    return np_unsupported(OP_LIST, "process list (proposed proc.list)");
}

static nxproc_status_t be_sys(np_raw_sys_t *o) {
    np_k_sysinfo_t info;
    np_zero(&info, sizeof(info));
    int64_t r = syscall1(NP_SYS_INFO, (int64_t)(uintptr_t)&info);
    if (r == -NP_ENOSYS) return np_unsupported(OP_SYSINFO, "system info (SYS_INFO)");
    if (r < 0) {
        if (!g_logged_unsupported[OP_SYSINFO]) {
            g_logged_unsupported[OP_SYSINFO] = 1;
            np_log("SYS_INFO failed", "");
        }
        return NXPROC_ERR_KERNEL;
    }
    if (info.total_memory == 0 || info.free_memory > info.total_memory) {
        if (!g_logged_unsupported[OP_SYSINFO]) {
            g_logged_unsupported[OP_SYSINFO] = 1;
            np_log("SYS_INFO returned inconsistent memory figures", "");
        }
        return NXPROC_ERR_KERNEL;
    }
    o->valid = NXPROC_SYS_MEMORY | NXPROC_SYS_UPTIME;
    o->now_ms = info.uptime_ms;
    o->cpus = info.num_cpus ? info.num_cpus : 1u;
    o->mem_total = info.total_memory;
    o->mem_free = info.free_memory;
    o->uptime_ms = info.uptime_ms;
    (void)np_unsupported(OP_CPU, "CPU load (proposed sys.stats)");
    return NXPROC_OK;
}

static nxproc_status_t be_end(uint32_t pid, nxproc_end_mode_t mode, uint32_t grace_ms) {
    (void)pid; (void)mode; (void)grace_ms;
    return np_unsupported(OP_END, "end process (proposed proc.kill)");
}

static nxproc_status_t be_prio(uint32_t pid, uint8_t prio) {
    (void)pid; (void)prio;
    return np_unsupported(OP_PRIO, "set priority (proposed proc.setprio)");
}

static nxproc_status_t be_startup(nxproc_startup_t *out, uint32_t cap, uint32_t *count) {
    (void)out; (void)cap;
    *count = 0;
    return np_unsupported(OP_STARTUP, "startup items (no registry yet)");
}

static uint32_t be_caps(void) {
    return NXPROC_CAP_SYS_MEM;
}

#endif /* NX_HOST_PREVIEW && !NXPROC_NO_DEMO */

/* ============ 3. Common code ============ */

static uint32_t g_cpus = 1;   /* last CPU count the kernel reported */

uint32_t nxproc_capabilities(void) { return be_caps(); }

nxproc_status_t nxproc_sampler_init(nxproc_sampler_t *s) {
    if (!s) return NXPROC_ERR_INVALID_ARG;
    np_zero(s, sizeof(*s));
    s->magic = NXPROC_SAMPLER_MAGIC;
    return NXPROC_OK;
}

nxproc_status_t nxproc_self(uint32_t *pid, uint32_t *ppid) {
    if (!pid || !ppid) return NXPROC_ERR_INVALID_ARG;
    return be_self(pid, ppid);
}

/* CPU share (x10, 0..1000) of delta_cpu over delta_wall on `cpus` cores. */
static uint16_t np_pct_x10(uint64_t delta_cpu, uint64_t delta_wall, uint32_t cpus) {
    if (delta_wall == 0 || cpus == 0) return 0;
    uint64_t denom = delta_wall * (uint64_t)cpus;
    uint64_t v = (delta_cpu * 1000u + denom / 2u) / denom;
    return (uint16_t)(v > 1000u ? 1000u : v);
}

nxproc_status_t nxproc_snapshot(nxproc_sampler_t *s, nxproc_snapshot_t *out) {
    if (!s || !out || s->magic != NXPROC_SAMPLER_MAGIC) return NXPROC_ERR_INVALID_ARG;
    out->count = 0;
    out->kernel_count = 0;
    out->taken_ms = 0;

    uint32_t count = 0, kcount = 0;
    uint64_t now = 0;
    nxproc_status_t st = be_list(out->procs, NXPROC_MAX_PROCS, &count, &kcount, &now);
    if (st != NXPROC_OK) return st;
    if (count > NXPROC_MAX_PROCS) {   /* defensive: never trust the count blindly */
        np_log("backend reported too many processes; clamped", "");
        count = NXPROC_MAX_PROCS;
    }
    if (kcount < count) kcount = count;

    uint32_t self = 0, self_pp = 0;
    int have_self = (be_self(&self, &self_pp) == NXPROC_OK);

    uint64_t dt = (s->prev_count > 0 && now > s->prev_taken_ms) ? now - s->prev_taken_ms : 0;
    g_known_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        nxproc_info_t *p = &out->procs[i];
        p->name[NXPROC_NAME_MAX - 1] = '\0';
        p->owner[NXPROC_OWNER_MAX - 1] = '\0';
        if ((int)p->state < 0 || p->state >= NXPROC_STATE_COUNT) p->state = NXPROC_STATE_UNKNOWN;
        if (have_self && p->pid == self) p->flags |= NXPROC_F_SELF;
        p->cpu_pct_x10 = 0;
        if (dt > 0) {
            for (uint32_t k = 0; k < s->prev_count; k++) {
                const nxproc_prev_t *q = &s->prev[k];
                if (q->pid == p->pid && q->start_ms == p->start_ms) {
                    if (p->cpu_time_ms >= q->cpu_time_ms)
                        p->cpu_pct_x10 = np_pct_x10(p->cpu_time_ms - q->cpu_time_ms, dt, g_cpus);
                    break;
                }
            }
        }
        g_known[g_known_count].pid = p->pid;
        g_known[g_known_count].flags = p->flags;
        g_known_count++;
    }
    /* Remember this sample for the next delta. */
    for (uint32_t i = 0; i < count; i++) {
        s->prev[i].pid = out->procs[i].pid;
        s->prev[i].start_ms = out->procs[i].start_ms;
        s->prev[i].cpu_time_ms = out->procs[i].cpu_time_ms;
    }
    s->prev_count = count;
    s->prev_taken_ms = now;

    out->count = count;
    out->kernel_count = kcount;
    out->taken_ms = now;
    return (kcount > count) ? NXPROC_ERR_TRUNCATED : NXPROC_OK;
}

nxproc_status_t nxproc_sys_stats(nxproc_sampler_t *s, nxproc_sys_stats_t *out) {
    if (!s || !out || s->magic != NXPROC_SAMPLER_MAGIC) return NXPROC_ERR_INVALID_ARG;
    np_zero(out, sizeof(*out));
    np_raw_sys_t raw;
    np_zero(&raw, sizeof(raw));
    nxproc_status_t st = be_sys(&raw);
    if (st != NXPROC_OK) return st;

    uint32_t cpus = raw.cpus ? raw.cpus : 1u;
    if (cpus > NXPROC_MAX_CPUS) cpus = NXPROC_MAX_CPUS;
    g_cpus = cpus;

    out->taken_ms = raw.now_ms;
    out->cpu_count = cpus;
    if (raw.valid & NXPROC_SYS_MEMORY) {
        out->mem_total = raw.mem_total;
        out->mem_free = raw.mem_free;
        out->mem_used = raw.mem_total - raw.mem_free;   /* be_sys checked free <= total */
        out->valid |= NXPROC_SYS_MEMORY;
    }
    if (raw.valid & NXPROC_SYS_UPTIME) {
        out->uptime_ms = raw.uptime_ms;
        out->valid |= NXPROC_SYS_UPTIME;
    }
    if (raw.valid & NXPROC_SYS_PROCS) {
        out->proc_count = raw.procs;
        out->thread_count = raw.threads;
        out->valid |= NXPROC_SYS_PROCS;
    }
    if (raw.valid & NXPROC_SYS_CPU) {
        if (s->have_sys_prev && raw.now_ms > s->prev_sys_ms && raw.busy_ms >= s->prev_busy_ms) {
            uint64_t dt = raw.now_ms - s->prev_sys_ms;
            out->cpu_pct_x10 = np_pct_x10(raw.busy_ms - s->prev_busy_ms, dt, cpus);
            out->valid |= NXPROC_SYS_CPU;
            if (raw.valid & NXPROC_SYS_PER_CORE) {
                for (uint32_t c = 0; c < cpus; c++) {
                    if (raw.core_busy_ms[c] >= s->prev_core_busy_ms[c])
                        out->core_pct_x10[c] = np_pct_x10(raw.core_busy_ms[c] - s->prev_core_busy_ms[c], dt, 1);
                }
                out->valid |= NXPROC_SYS_PER_CORE;
            }
        }
        s->prev_sys_ms = raw.now_ms;
        s->prev_busy_ms = raw.busy_ms;
        for (uint32_t c = 0; c < cpus; c++) s->prev_core_busy_ms[c] = raw.core_busy_ms[c];
        s->have_sys_prev = 1;
    }
    return out->valid ? NXPROC_OK : NXPROC_ERR_UNSUPPORTED;
}

/* Client-side refusal before any syscall. The kernel re-checks. */
static nxproc_status_t np_precheck(uint32_t pid) {
    if (pid == 0) return NXPROC_ERR_INVALID_ARG;
    uint32_t self = 0, pp = 0;
    if (be_self(&self, &pp) == NXPROC_OK && pid == self) return NXPROC_ERR_PROTECTED;
    for (uint32_t i = 0; i < g_known_count; i++) {
        if (g_known[i].pid == pid) {
            if (g_known[i].flags & (NXPROC_F_SYSTEM | NXPROC_F_KERNEL)) return NXPROC_ERR_PROTECTED;
            break;
        }
    }
    return NXPROC_OK;
}

static void np_log_pid(const char *what, uint32_t pid, nxproc_status_t st) {
    char num[16];
    char msg[96];
    np_u64(num, sizeof(num), pid);
    np_copy(msg, sizeof(msg), what);
    uint32_t n = 0;
    while (msg[n]) n++;
    for (const char *s = num; *s && n + 1 < sizeof(msg); s++) msg[n++] = *s;
    for (const char *s = " -> "; *s && n + 1 < sizeof(msg); s++) msg[n++] = *s;
    for (const char *s = nxproc_status_str(st); *s && n + 1 < sizeof(msg); s++) msg[n++] = *s;
    msg[n] = '\0';
    np_log(msg, "");
}

nxproc_status_t nxproc_end(uint32_t pid, nxproc_end_mode_t mode, uint32_t grace_ms) {
    if (mode != NXPROC_END_GRACEFUL && mode != NXPROC_END_FORCE) return NXPROC_ERR_INVALID_ARG;
    if (grace_ms == 0 || grace_ms > 60000u) grace_ms = NXPROC_GRACE_MS;
    nxproc_status_t st = np_precheck(pid);
    if (st == NXPROC_OK) st = be_end(pid, mode, grace_ms);
    np_log_pid(mode == NXPROC_END_FORCE ? "force end pid " : "end request pid ", pid, st);
    return st;
}

nxproc_status_t nxproc_set_priority(uint32_t pid, uint8_t priority) {
    nxproc_status_t st = np_precheck(pid);
    if (st == NXPROC_OK) st = be_prio(pid, priority);
    np_log_pid("set priority pid ", pid, st);
    return st;
}

nxproc_status_t nxproc_startup_list(nxproc_startup_t *out, uint32_t cap, uint32_t *count) {
    if (!out || !count || cap == 0) return NXPROC_ERR_INVALID_ARG;
    *count = 0;
    if (cap > NXPROC_MAX_STARTUP) cap = NXPROC_MAX_STARTUP;
    return be_startup(out, cap, count);
}

const char *nxproc_status_str(nxproc_status_t st) {
    switch (st) {
        case NXPROC_OK:              return "ok";
        case NXPROC_ERR_INVALID_ARG: return "invalid argument";
        case NXPROC_ERR_UNSUPPORTED: return "not supported by this kernel";
        case NXPROC_ERR_NOT_FOUND:   return "no such process";
        case NXPROC_ERR_PERMISSION:  return "permission denied";
        case NXPROC_ERR_PROTECTED:   return "protected process";
        case NXPROC_ERR_KERNEL:      return "kernel error";
        case NXPROC_ERR_TRUNCATED:   return "list truncated";
        default:                     return "unknown error";
    }
}

const char *nxproc_state_name(nxproc_state_t st) {
    switch (st) {
        case NXPROC_STATE_RUNNING:  return "Running";
        case NXPROC_STATE_READY:    return "Ready";
        case NXPROC_STATE_BLOCKED:  return "Waiting";
        case NXPROC_STATE_SLEEPING: return "Sleeping";
        case NXPROC_STATE_ZOMBIE:   return "Exiting";
        default:                    return "Unknown";
    }
}

const char *nxproc_priority_name(uint8_t p) {
    if (p < NXPROC_PRIO_HIGH)   return "Realtime";
    if (p < NXPROC_PRIO_ABOVE)  return "High";
    if (p < NXPROC_PRIO_NORMAL) return "Above normal";
    if (p < NXPROC_PRIO_BELOW)  return "Normal";
    if (p < NXPROC_PRIO_LOW)    return "Below normal";
    return "Low";
}
