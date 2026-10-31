/*
 * NeolyxOS - Task Manager (shell-hosted app view, see app_view.h)
 *
 * Tabs:
 *   Processes    sortable, filterable process table with summary tiles
 *   Performance  live CPU and memory area charts (60 s), cores, details
 *   Apps         the desktop's own windows; End Task closes the window
 *   Startup      items launched at login
 *
 * Data comes from nxproc (desktop/lib/nxproc.c). Where the running kernel
 * has no userspace path, nxproc returns NXPROC_ERR_UNSUPPORTED and this
 * view shows an honest "unavailable" state instead of invented numbers.
 * The kernel is sampled about once a second, never per frame; frames only
 * animate (graph scroll, sheet slide) between samples.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "app_view.h"
#include "nxui.h"
#include "nxglass.h"
#include "nxframe.h"
#include "nxicons.h"
#include "nxproc.h"
#include "nxsyscall.h"

#ifdef NX_HOST_PREVIEW
#ifndef NXPROC_NO_DEMO
/* Preview-only: fast-forwards nxproc's demo clock (lib/nxproc.c). */
void nxproc_demo_advance(uint32_t ms);
#endif
/* Preview-only script helpers ("call taskmgr_preview_light"). */
void taskmgr_preview_light(void);
void taskmgr_preview_light(void) { nxui_set_appearance(NXUI_APPEARANCE_LIGHT); }
#endif

/* ============ Constants ============ */

#define TM_REFRESH_MS   1000u
#define TM_HIST_N       62          /* 61 samples span 60 s, +1 sliding in */
#define TM_ROW_H        26.0f
#define TM_HEAD_H       28.0f
#define TM_PAD          16.0f
#define TM_TOOLBAR_H    52.0f
#define TM_FOOTER_H     50.0f
#define TM_SEARCH_MAX   32
#define TM_STATUS_MS    4500u

enum { TAB_PROC = 0, TAB_PERF, TAB_APPS, TAB_START, TAB_COUNT };
static const char *const k_tabs[TAB_COUNT] = { "Processes", "Performance", "Apps", "Startup" };

/* Process columns */
enum { PC_NAME = 0, PC_PID, PC_STATE, PC_OWNER, PC_THREADS, PC_CPU, PC_MEM, PC_COUNT };
typedef struct { const char *title; float width; int right; int desc_first; } tm_col_t;
static const tm_col_t k_pcols[PC_COUNT] = {
    { "Name",    0.0f,  0, 0 },     /* flexible */
    { "PID",     58.0f, 1, 0 },
    { "State",   84.0f, 0, 0 },
    { "User",    66.0f, 0, 0 },
    { "Threads", 66.0f, 1, 1 },
    { "CPU",     74.0f, 1, 1 },
    { "Memory",  92.0f, 1, 1 },
};

enum { AC_NAME = 0, AC_STATUS, AC_SIZE, AC_ID, AC_COUNT };
static const tm_col_t k_acols[AC_COUNT] = {
    { "Window", 0.0f, 0, 0 }, { "Status", 120.0f, 0, 0 },
    { "Size", 110.0f, 1, 0 }, { "Window ID", 92.0f, 1, 0 },
};

enum { SC_NAME = 0, SC_PATH, SC_STATUS, SC_IMPACT, SC_COUNT };
static const tm_col_t k_scols[SC_COUNT] = {
    { "Name", 0.0f, 0, 0 }, { "Path", 250.0f, 0, 0 },
    { "Status", 96.0f, 0, 0 }, { "Impact", 90.0f, 0, 0 },
};

static const struct { const char *name; uint8_t value; } k_prios[] = {
    { "High",         NXPROC_PRIO_HIGH },
    { "Above normal", NXPROC_PRIO_ABOVE },
    { "Normal",       NXPROC_PRIO_NORMAL },
    { "Below normal", NXPROC_PRIO_BELOW },
    { "Low",          NXPROC_PRIO_LOW },
};
#define TM_NPRIO ((int)(sizeof(k_prios) / sizeof(k_prios[0])))

/* ============ State ============ */

typedef struct {
    uint32_t win_id;
    int      tab, hover_tab;
    uint32_t caps;

    /* Sampling (kernel data, refreshed ~1 Hz) */
    nxproc_sampler_t   sampler;
    nxproc_snapshot_t  snap;
    nxproc_status_t    snap_st;
    nxproc_sys_stats_t sys;
    nxproc_status_t    sys_st;
    uint64_t next_refresh_ms;
    uint64_t last_sample_ms;          /* wall clock (nx_gettime) of the last sample */
    uint32_t self_pid;

    /* 60 s history (oldest first) */
    float    cpu_hist[TM_HIST_N];
    float    mem_hist[TM_HIST_N];
    int      cpu_n, mem_n;

    /* Processes table */
    int      sort_col, sort_desc;
    uint16_t order[NXPROC_MAX_PROCS];
    int      nvis;
    uint32_t sel_pid;
    int      scroll;

    /* Apps table */
    uint32_t sel_win;
    int      app_scroll;

    /* Startup */
    nxproc_startup_t startup[NXPROC_MAX_STARTUP];
    uint32_t startup_n;
    nxproc_status_t startup_st;
    int      sel_start;

    /* Search */
    char     search[TM_SEARCH_MAX + 1];
    int      search_len;
    int      search_focus;

    /* Pointer (absolute) */
    int32_t  mx, my;
    int      pointer_in;
    int      pressed_btn;             /* footer button held: 1 end, 2 prio, 3 switch */

    /* Keyboard modifiers */
    int      shift, ctrl;

    /* End Task confirmation sheet */
    int      sheet_open;
    float    sheet_t;
    int      sheet_is_app;
    uint32_t sheet_pid, sheet_win;
    char     sheet_name[NXPROC_NAME_MAX];
    int      sheet_hover;             /* 1 cancel, 2 confirm */

    /* Graceful end in progress: force after the grace period */
    uint32_t pending_pid;
    uint64_t pending_deadline_ms;
    char     pending_name[NXPROC_NAME_MAX];

    /* Priority popover */
    int      prio_open;

    /* Footer status line */
    char     status[112];
    int      status_err;
    uint64_t status_until_ms;
} tm_state_t;

static tm_state_t g_tm;

/* ============ Small string helpers (freestanding) ============ */

typedef struct { char *buf; uint32_t cap, len; } tm_sb_t;

static void sb_init(tm_sb_t *b, char *buf, uint32_t cap) {
    b->buf = buf; b->cap = cap; b->len = 0;
    if (cap) buf[0] = '\0';
}
static void sb_str(tm_sb_t *b, const char *s) {
    if (!b->buf || b->cap == 0) return;
    while (s && *s && b->len + 1 < b->cap) b->buf[b->len++] = *s++;
    b->buf[b->len] = '\0';
}
static void sb_u64(tm_sb_t *b, uint64_t v) {
    char t[24]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 23);
    char o[24]; int k = 0;
    while (n > 0) o[k++] = t[--n];
    o[k] = '\0';
    sb_str(b, o);
}
/* value x10 as "12.3" */
static void sb_x10(tm_sb_t *b, uint64_t v10) {
    sb_u64(b, v10 / 10u);
    char d[3] = { '.', (char)('0' + v10 % 10u), '\0' };
    sb_str(b, d);
}
static void sb_bytes(tm_sb_t *b, uint64_t bytes) {
    const uint64_t KB = 1024u, MB = KB * 1024u, GB = MB * 1024u;
    if (bytes >= GB)      { sb_x10(b, (bytes * 10u + GB / 2u) / GB); sb_str(b, " GB"); }
    else if (bytes >= MB) { sb_x10(b, (bytes * 10u + MB / 2u) / MB); sb_str(b, " MB"); }
    else                  { sb_u64(b, (bytes + KB / 2u) / KB); sb_str(b, " KB"); }
}
static void sb_duration(tm_sb_t *b, uint64_t ms) {
    uint64_t s = ms / 1000u, d = s / 86400u, h = (s / 3600u) % 24u, m = (s / 60u) % 60u;
    if (d) { sb_u64(b, d); sb_str(b, " d "); sb_u64(b, h); sb_str(b, " h"); }
    else if (h) { sb_u64(b, h); sb_str(b, " h "); sb_u64(b, m); sb_str(b, " min"); }
    else if (m) { sb_u64(b, m); sb_str(b, " min "); sb_u64(b, s % 60u); sb_str(b, " s"); }
    else { sb_u64(b, s); sb_str(b, " s"); }
}

static char tm_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int tm_strcmp_ci(const char *a, const char *b) {
    while (*a && *b) {
        char x = tm_lower(*a), y = tm_lower(*b);
        if (x != y) return (x < y) ? -1 : 1;
        a++; b++;
    }
    return (*a == *b) ? 0 : (*a ? 1 : -1);
}

static int tm_contains_ci(const char *hay, const char *needle) {
    if (!needle || !needle[0]) return 1;
    if (!hay) return 0;
    for (const char *h = hay; *h; h++) {
        const char *x = h, *y = needle;
        while (*x && *y && tm_lower(*x) == tm_lower(*y)) { x++; y++; }
        if (!*y) return 1;
    }
    return 0;
}

static void tm_copy(char *dst, uint32_t cap, const char *src) {
    tm_sb_t b; sb_init(&b, dst, cap); sb_str(&b, src);
}

static void tm_log(const char *msg) {
    char line[128];
    tm_sb_t b; sb_init(&b, line, sizeof(line));
    sb_str(&b, "[TASKMGR] "); sb_str(&b, msg); sb_str(&b, "\n");
    nx_debug_print(line);
}

static void tm_set_status(const char *msg, int err) {
    tm_copy(g_tm.status, sizeof(g_tm.status), msg);
    g_tm.status_err = err;
    g_tm.status_until_ms = nx_gettime() + TM_STATUS_MS;
}

/* ============ Sampling ============ */

static const nxproc_info_t *tm_find_pid(uint32_t pid) {
    for (uint32_t i = 0; i < g_tm.snap.count; i++)
        if (g_tm.snap.procs[i].pid == pid) return &g_tm.snap.procs[i];
    return 0;
}

static int tm_cmp(const nxproc_info_t *a, const nxproc_info_t *b, int col) {
    int r = 0;
    switch (col) {
        case PC_NAME:    r = tm_strcmp_ci(a->name, b->name); break;
        case PC_PID:     r = (a->pid > b->pid) - (a->pid < b->pid); break;
        case PC_STATE:   r = tm_strcmp_ci(nxproc_state_name(a->state), nxproc_state_name(b->state)); break;
        case PC_OWNER:   r = tm_strcmp_ci(a->owner, b->owner); break;
        case PC_THREADS: r = (a->threads > b->threads) - (a->threads < b->threads); break;
        case PC_CPU:     r = (a->cpu_pct_x10 > b->cpu_pct_x10) - (a->cpu_pct_x10 < b->cpu_pct_x10);
                         if (!r) r = (a->cpu_time_ms > b->cpu_time_ms) - (a->cpu_time_ms < b->cpu_time_ms);
                         break;
        case PC_MEM:     r = (a->resident_bytes > b->resident_bytes) - (a->resident_bytes < b->resident_bytes); break;
        default: break;
    }
    return r;
}

/* Rebuild the filtered + sorted row order (on sample, sort or search change). */
static void tm_rebuild_order(void) {
    int n = 0;
    int numeric = g_tm.search_len > 0;
    uint32_t want_pid = 0;
    for (int i = 0; i < g_tm.search_len; i++) {
        char ch = g_tm.search[i];
        if (ch < '0' || ch > '9') { numeric = 0; break; }
        want_pid = want_pid * 10u + (uint32_t)(ch - '0');
        if (want_pid > 100000000u) { numeric = 0; break; }
    }
    for (uint32_t i = 0; i < g_tm.snap.count && n < NXPROC_MAX_PROCS; i++) {
        const nxproc_info_t *p = &g_tm.snap.procs[i];
        if (g_tm.search_len > 0 && !tm_contains_ci(p->name, g_tm.search) &&
            !(numeric && p->pid == want_pid)) continue;
        g_tm.order[n++] = (uint16_t)i;
    }
    /* Insertion sort: n <= 256, stable, runs at most once per second. */
    for (int i = 1; i < n; i++) {
        uint16_t key = g_tm.order[i];
        const nxproc_info_t *kp = &g_tm.snap.procs[key];
        int j = i - 1;
        while (j >= 0) {
            const nxproc_info_t *jp = &g_tm.snap.procs[g_tm.order[j]];
            int c = tm_cmp(jp, kp, g_tm.sort_col);
            if (g_tm.sort_desc) c = -c;
            if (c == 0) c = (jp->pid > kp->pid) - (jp->pid < kp->pid);
            if (c <= 0) break;
            g_tm.order[j + 1] = g_tm.order[j];
            j--;
        }
        g_tm.order[j + 1] = key;
    }
    g_tm.nvis = n;
}

static void tm_hist_push(float *hist, int *n, float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (*n < TM_HIST_N) { hist[(*n)++] = v; return; }
    for (int i = 1; i < TM_HIST_N; i++) hist[i - 1] = hist[i];
    hist[TM_HIST_N - 1] = v;
}

static void tm_check_pending(void) {
    if (!g_tm.pending_pid || g_tm.snap_st == NXPROC_ERR_UNSUPPORTED) return;
    char msg[112];
    tm_sb_t b; sb_init(&b, msg, sizeof(msg));
    if (!tm_find_pid(g_tm.pending_pid)) {
        sb_str(&b, g_tm.pending_name); sb_str(&b, " has ended.");
        tm_set_status(msg, 0);
        g_tm.pending_pid = 0;
        return;
    }
    if (nx_gettime() >= g_tm.pending_deadline_ms) {
        nxproc_status_t st = nxproc_end(g_tm.pending_pid, NXPROC_END_FORCE, 0);
        if (st == NXPROC_OK) {
            sb_str(&b, g_tm.pending_name); sb_str(&b, " did not respond and was forced to quit.");
            tm_set_status(msg, 0);
        } else {
            sb_str(&b, "Couldn't force "); sb_str(&b, g_tm.pending_name);
            sb_str(&b, " to quit: "); sb_str(&b, nxproc_status_str(st));
            tm_set_status(msg, 1);
        }
        g_tm.pending_pid = 0;
    }
}

static void tm_sample(void) {
    g_tm.snap_st = nxproc_snapshot(&g_tm.sampler, &g_tm.snap);
    if (g_tm.snap_st != NXPROC_OK && g_tm.snap_st != NXPROC_ERR_TRUNCATED) g_tm.snap.count = 0;
    g_tm.sys_st = nxproc_sys_stats(&g_tm.sampler, &g_tm.sys);
    if (g_tm.sys_st == NXPROC_OK) {
        if (g_tm.sys.valid & NXPROC_SYS_CPU)
            tm_hist_push(g_tm.cpu_hist, &g_tm.cpu_n, (float)g_tm.sys.cpu_pct_x10 / 1000.0f);
        if ((g_tm.sys.valid & NXPROC_SYS_MEMORY) && g_tm.sys.mem_total)
            tm_hist_push(g_tm.mem_hist, &g_tm.mem_n, (float)((double)g_tm.sys.mem_used / (double)g_tm.sys.mem_total));
    }
    if (g_tm.sel_pid && !tm_find_pid(g_tm.sel_pid)) { g_tm.sel_pid = 0; g_tm.prio_open = 0; }
    tm_rebuild_order();
    tm_check_pending();
    uint64_t now = nx_gettime();
    g_tm.last_sample_ms = now;
    g_tm.next_refresh_ms = now + TM_REFRESH_MS;
}

/* ============ Layout ============ */

typedef struct {
    nxc_rect_t content, tabs, search, body, footer;
} tm_layout_t;

static tm_layout_t tm_layout(const desktop_window_t *win) {
    tm_layout_t L;
    nxc_rect_t c = app_view_content_rect(win);
    L.content = c;
    L.tabs = nxc_rect(c.x + TM_PAD, c.y + 12.0f, 400.0f, 28.0f);
    L.search = nxc_rect(c.x + c.w - TM_PAD - 210.0f, c.y + 12.0f, 210.0f, 28.0f);
    L.body = nxc_rect(c.x + TM_PAD, c.y + TM_TOOLBAR_H, c.w - 2.0f * TM_PAD,
                      c.h - TM_TOOLBAR_H - TM_FOOTER_H);
    L.footer = nxc_rect(c.x, c.y + c.h - TM_FOOTER_H, c.w, TM_FOOTER_H);
    return L;
}

static int tm_has_search(void) { return g_tm.tab != TAB_PERF; }

/* Table geometry for the active tab */
typedef struct {
    nxc_rect_t head, rows;
    float      colx[8], colw[8];
    int        ncols;
    int        visible_rows;
} tm_table_t;

static tm_table_t tm_table(const tm_layout_t *L, const tm_col_t *cols, int ncols, float top) {
    tm_table_t T;
    float x = L->body.x, w = L->body.w;
    T.head = nxc_rect(x, top, w, TM_HEAD_H);
    T.rows = nxc_rect(x, top + TM_HEAD_H + 2.0f, w, L->body.y + L->body.h - (top + TM_HEAD_H + 2.0f) - 4.0f);
    T.ncols = ncols;
    float fixed = 0.0f;
    for (int i = 0; i < ncols; i++) fixed += cols[i].width;
    float cx = x + 8.0f;
    float avail = w - 16.0f;
    for (int i = 0; i < ncols && i < 8; i++) {
        float cw = cols[i].width > 0.0f ? cols[i].width : (avail - fixed);
        if (cw < 40.0f) cw = 40.0f;
        T.colx[i] = cx;
        T.colw[i] = cw;
        cx += cw;
    }
    T.visible_rows = (int)(T.rows.h / TM_ROW_H);
    if (T.visible_rows < 1) T.visible_rows = 1;
    return T;
}

static float tm_proc_table_top(const tm_layout_t *L) { return L->body.y + 70.0f; }

static nxc_rect_t tm_footer_btn(const tm_layout_t *L, int which) {
    /* 1 = End Task (rightmost), 2 = Priority / Switch To (left of it) */
    float w = 104.0f, h = NXGLASS_BUTTON_H;
    float y = L->footer.y + (L->footer.h - h) * 0.5f;
    float x = L->footer.x + L->footer.w - TM_PAD - w;
    if (which == 2) x -= w + 10.0f;
    return nxc_rect(x, y, w, h);
}

static nxc_rect_t tm_sheet_rect(const tm_layout_t *L) {
    float w = 420.0f, h = 148.0f;
    float y = L->content.y + 18.0f - (1.0f - nxui_ease_out_cubic(g_tm.sheet_t)) * 30.0f;
    return nxc_rect(L->content.x + (L->content.w - w) * 0.5f, y, w, h);
}

static nxc_rect_t tm_sheet_btn(nxc_rect_t s, int which) {
    float w = 104.0f, y = s.y + s.h - 18.0f - NXGLASS_BUTTON_H;
    float x = s.x + s.w - 20.0f - w;            /* 2 = confirm (right) */
    if (which == 1) x -= w + 10.0f;              /* 1 = cancel */
    return nxc_rect(x, y, w, NXGLASS_BUTTON_H);
}

static nxc_rect_t tm_prio_rect(const tm_layout_t *L) {
    nxc_rect_t b = tm_footer_btn(L, 2);
    float h = 12.0f + (float)TM_NPRIO * 28.0f;
    return nxc_rect(b.x - 30.0f, b.y - h - 8.0f, 164.0f, h);
}

/* ============ Selection helpers ============ */

static const nxproc_info_t *tm_selected(void) {
    return g_tm.sel_pid ? tm_find_pid(g_tm.sel_pid) : 0;
}

static int tm_proc_endable(const nxproc_info_t *p) {
    if (!p) return 0;
    if (!(g_tm.caps & NXPROC_CAP_END)) return 0;
    return !(p->flags & (NXPROC_F_SYSTEM | NXPROC_F_KERNEL | NXPROC_F_SELF));
}

/* Apps: list of window slots (filtered); returns count, fills ids. */
static int tm_apps(uint32_t *ids, int cap) {
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS && n < cap; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (!w || w->id == 0) continue;
        if (g_tm.search_len && !tm_contains_ci(w->title, g_tm.search)) continue;
        ids[n++] = w->id;
    }
    return n;
}

static const desktop_window_t *tm_window(uint32_t id) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (w && w->id == id) return w;
    }
    return 0;
}

/* ============ Drawing primitives ============ */

/* Scale a colour's alpha (keeps theme-defined translucency in both appearances). */
static nxc_color_t tm_fade(nxc_color_t c, float k) {
    float a = (float)((c >> 24) & 0xFFu) * k;
    if (a < 0.0f) a = 0.0f;
    if (a > 255.0f) a = 255.0f;
    return NXC_WITH_ALPHA(c, (uint8_t)a);
}

static nxc_color_t tm_mem_color(void) {
    return nxui_appearance() == NXUI_APPEARANCE_LIGHT ? NXC_RGB(0x3B, 0x6F, 0xE0) : NXC_RGB(0x5B, 0x9C, 0xFF);
}

static nxc_color_t tm_cpu_color(void) { return nxui_theme()->accent; }

/* Badge with the first letter of a name, tinted by a hash of it. */
static void tm_initial_icon(nxc_canvas_t *c, float x, float cy, float size, const char *name, int system) {
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t r = nxc_rect(x, cy - size * 0.5f, size, size);
    if (system) {
        nxc_fill_rrect(c, r, size * 0.28f, th->control_fill_hover);
        nxsym_draw(c, NXSYM_GEAR, x + size * 0.5f, cy, size * 0.68f, th->text_secondary, 0.0f);
        return;
    }
    static const uint32_t hues[] = { 0xFFE8660A, 0xFF3B82F6, 0xFF10B981, 0xFF8B5CF6, 0xFFEC4899, 0xFFF59E0B, 0xFF06B6D4 };
    uint32_t h = 2166136261u;
    for (const char *s = name; s && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    nxc_color_t col = (nxc_color_t)hues[h % (sizeof(hues) / sizeof(hues[0]))];
    nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x, r.y + r.h, col, NXC_WITH_ALPHA(col, 200));
    nxc_fill_rrect_paint(c, r, size * 0.28f, &p);
    char ini[2] = { name && name[0] ? name[0] : '?', '\0' };
    if (ini[0] >= 'a' && ini[0] <= 'z') ini[0] = (char)(ini[0] - 32);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, r, ini, NXC_RGB(0xFF, 0xFF, 0xFF), NXC_ALIGN_CENTER);
}

/* Smooth area chart of hist[0..n) (values 0..1, oldest first) over 60 s.
 * `frac` (0..1) slides the series left between samples. */
static void tm_area_chart(nxc_canvas_t *c, nxc_rect_t r, const float *hist, int n,
                          float frac, nxc_color_t color, int grid) {
    const nxui_theme_t *th = nxui_theme();
    if (grid) {
        for (int k = 1; k < 4; k++)
            nxc_fill_rect(c, nxc_rect(r.x, r.y + r.h * (float)k / 4.0f, r.w, 1.0f),
                          tm_fade(th->separator, 0.7f));
    }
    if (n < 2) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    float step = r.w / (float)(TM_HIST_N - 2);
    nxc_point_t pt[TM_HIST_N];
    for (int k = 0; k < n; k++) {
        float age = (float)(n - 1 - k) + frac - 1.0f;     /* newest slides in from the right */
        float v = hist[k];
        pt[k].x = r.x + r.w - age * step;
        pt[k].y = r.y + r.h - v * (r.h - 2.0f) - 1.0f;
    }

    static nxc_point_t pbuf[TM_HIST_N * 3 + 8];
    static uint8_t     vbuf[TM_HIST_N + 8];
    nxc_path_t line, area;
    float lo = r.y + 1.0f, hi = r.y + r.h - 1.0f;

    for (int pass = 0; pass < 2; pass++) {
        nxc_path_t *p = pass == 0 ? &area : &line;
        if (nxc_path_init(p, pbuf, (uint32_t)(sizeof(pbuf) / sizeof(pbuf[0])), vbuf,
                          (uint32_t)sizeof(vbuf)) != NXC_OK) return;
        nxc_path_move_to(p, pt[0].x, pt[0].y);
        for (int k = 0; k + 1 < n; k++) {
            nxc_point_t p0 = pt[k > 0 ? k - 1 : k], p1 = pt[k], p2 = pt[k + 1];
            nxc_point_t p3 = pt[k + 2 < n ? k + 2 : k + 1];
            float c1x = p1.x + (p2.x - p0.x) / 6.0f, c1y = p1.y + (p2.y - p0.y) / 6.0f;
            float c2x = p2.x - (p3.x - p1.x) / 6.0f, c2y = p2.y - (p3.y - p1.y) / 6.0f;
            if (c1y < lo) c1y = lo;
            if (c1y > hi) c1y = hi;
            if (c2y < lo) c2y = lo;
            if (c2y > hi) c2y = hi;
            nxc_path_cubic_to(p, c1x, c1y, c2x, c2y, p2.x, p2.y);
        }
        if (pass == 0) {
            nxc_path_line_to(p, pt[n - 1].x, r.y + r.h);
            nxc_path_line_to(p, pt[0].x, r.y + r.h);
            nxc_path_close(p);
            nxc_paint_t paint = nxc_paint_linear(0.0f, r.y, 0.0f, r.y + r.h,
                                                 NXC_WITH_ALPHA(color, 110), NXC_WITH_ALPHA(color, 8));
            nxc_fill_path_paint(c, p, NXC_FILL_NONZERO, &paint);
        } else {
            nxc_stroke_t s = { 1.75f, NXC_CAP_ROUND, NXC_JOIN_ROUND, 4.0f };
            nxc_stroke_path(c, p, &s, color);
        }
    }
}

static float tm_hist_frac(void) {
    uint64_t now = nx_gettime();
    if (now <= g_tm.last_sample_ms) return 0.0f;
    float f = (float)(now - g_tm.last_sample_ms) / (float)TM_REFRESH_MS;
    return f > 1.0f ? 1.0f : f;
}

/* Wrap text into at most max_lines lines inside width w; draws them. */
static float tm_wrap_al(nxc_canvas_t *c, nxui_text_style_t style, float x, float y, float w,
                        float line_h, const char *text, nxc_color_t col, int max_lines,
                        nxc_align_t align) {
    char line[160];
    const char *s = text;
    int lines = 0;
    while (s && *s && lines < max_lines) {
        uint32_t best = 0, i = 0;
        while (s[i] && i + 1 < sizeof(line)) {
            line[i] = s[i];
            line[i + 1] = '\0';
            if (s[i] == ' ' || s[i + 1] == '\0') {
                uint32_t end = (s[i] == ' ') ? i : i + 1;
                line[end] = '\0';
                if (nxui_text_width(style, line) <= w || best == 0) best = end;
                else break;
                line[end] = s[i];
                line[i + 1] = '\0';
            }
            i++;
        }
        if (best == 0) best = i;
        for (uint32_t k = 0; k < best; k++) line[k] = s[k];
        line[best] = '\0';
        nxui_text_in_rect(c, style, nxc_rect(x, y, w, line_h), line, col, align);
        y += line_h;
        lines++;
        s += best;
        while (*s == ' ') s++;
    }
    return y;
}

static float tm_wrap(nxc_canvas_t *c, nxui_text_style_t style, float x, float y, float w,
                     float line_h, const char *text, nxc_color_t col, int max_lines) {
    return tm_wrap_al(c, style, x, y, w, line_h, text, col, max_lines, NXC_ALIGN_LEFT);
}

static void tm_header(nxc_canvas_t *c, const tm_table_t *T, const tm_col_t *cols, int sort_col,
                      int sort_desc, int sortable) {
    const nxui_theme_t *th = nxui_theme();
    for (int i = 0; i < T->ncols; i++) {
        nxc_rect_t cr = nxc_rect(T->colx[i] + 6.0f, T->head.y, T->colw[i] - 12.0f, T->head.h);
        int active = sortable && i == sort_col;
        int hover = sortable && g_tm.pointer_in && !g_tm.sheet_open &&
                    nxglass_hit(nxc_rect(T->colx[i], T->head.y, T->colw[i], T->head.h), g_tm.mx, g_tm.my);
        nxc_color_t col = active ? th->text_primary : (hover ? th->text_primary : th->text_secondary);
        nxui_text_in_rect(c, active ? NXUI_TEXT_HEADLINE : NXUI_TEXT_BODY_MEDIUM, cr, cols[i].title, col,
                          cols[i].right ? NXC_ALIGN_RIGHT : NXC_ALIGN_LEFT);
        if (active) {
            float tw = nxui_text_width(NXUI_TEXT_HEADLINE, cols[i].title);
            float ax = cols[i].right ? cr.x + cr.w - tw - 9.0f : cr.x + tw + 9.0f;
            nxsym_draw(c, sort_desc ? NXSYM_CHEVRON_DOWN : NXSYM_CHEVRON_UP, ax, cr.y + cr.h * 0.5f,
                       10.0f, th->accent, 0.0f);
        }
        if (i > 0)
            nxc_fill_rect(c, nxc_rect(T->colx[i], T->head.y + 8.0f, 1.0f, T->head.h - 16.0f),
                          tm_fade(th->separator, 0.8f));
    }
    nxglass_separator(c, T->head.x, T->head.y + T->head.h, T->head.w);
}

static void tm_row_bg(nxc_canvas_t *c, nxc_rect_t r, int selected, int hover) {
    const nxui_theme_t *th = nxui_theme();
    if (selected) nxc_fill_rrect(c, r, th->radius_control, th->selection);
    else if (hover) nxc_fill_rrect(c, r, th->radius_control, tm_fade(th->control_fill_hover, 0.75f));
}

static void tm_cell(nxc_canvas_t *c, const tm_table_t *T, const tm_col_t *cols, int i, float y,
                    const char *text, nxc_color_t col) {
    nxc_rect_t cr = nxc_rect(T->colx[i] + 6.0f, y, T->colw[i] - 12.0f, TM_ROW_H);
    nxui_text_in_rect(c, NXUI_TEXT_BODY, cr, text, col, cols[i].right ? NXC_ALIGN_RIGHT : NXC_ALIGN_LEFT);
}

static void tm_scrollbar(nxc_canvas_t *c, nxc_rect_t rows, int total, int visible, int scroll) {
    if (total <= visible) return;
    float track_h = rows.h - 4.0f;
    float h = track_h * (float)visible / (float)total;
    if (h < 24.0f) h = 24.0f;
    float y = rows.y + 2.0f + (track_h - h) * (float)scroll / (float)(total - visible);
    nxc_fill_rrect(c, nxc_rect(rows.x + rows.w - 6.0f, y, 4.0f, h), 2.0f,
                   NXC_WITH_ALPHA(nxui_theme()->text_secondary, 110));
}

/* An honest empty/unavailable state inside r. */
static void tm_unavailable(nxc_canvas_t *c, nxc_rect_t r, const char *title, const char *body) {
    const nxui_theme_t *th = nxui_theme();
    float w = 440.0f;
    if (w > r.w - 40.0f) w = r.w - 40.0f;
    float cx = r.x + r.w * 0.5f;
    float y = r.y + r.h * 0.5f - 62.0f;
    nxc_fill_circle(c, cx, y + 20.0f, 22.0f, NXC_WITH_ALPHA(th->warning, 40));
    nxsym_draw(c, NXSYM_INFO, cx, y + 20.0f, 22.0f, th->warning, 0.0f);
    y += 52.0f;
    nxui_text_in_rect(c, NXUI_TEXT_TITLE3, nxc_rect(r.x, y, r.w, 22.0f), title, th->text_primary, NXC_ALIGN_CENTER);
    y += 28.0f;
    tm_wrap_al(c, NXUI_TEXT_FOOTNOTE, cx - w * 0.5f + 20.0f, y, w - 40.0f, 18.0f, body,
               th->text_secondary, 4, NXC_ALIGN_CENTER);
}

/* ============ Stat tiles ============ */

static void tm_tile(nxc_canvas_t *c, nxc_rect_t r, const char *label, const char *value,
                    const char *sub, nxc_color_t accent, float bar, const float *hist, int hn) {
    const nxui_theme_t *th = nxui_theme();
    nxglass_card(c, r, 10.0f);
    nxc_fill_circle(c, r.x + 15.0f, r.y + 16.0f, 3.5f, accent);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(r.x + 24.0f, r.y + 8.0f, r.w - 32.0f, 16.0f),
                      label, th->text_secondary, NXC_ALIGN_LEFT);
    nxui_text_in_rect(c, NXUI_TEXT_TITLE2, nxc_rect(r.x + 12.0f, r.y + 24.0f, r.w * 0.62f, 24.0f),
                      value, th->text_primary, NXC_ALIGN_LEFT);
    if (sub)
        nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(r.x + 12.0f, r.y + 44.0f, r.w - 24.0f, 14.0f),
                          sub, th->text_tertiary, NXC_ALIGN_LEFT);
    if (hist && hn >= 2) {
        nxc_rect_t g = nxc_rect(r.x + r.w * 0.56f, r.y + 22.0f, r.w * 0.44f - 10.0f, r.h - 30.0f);
        nxc_save(c);
        nxc_clip_rrect(c, g, 2.0f);
        tm_area_chart(c, g, hist, hn, tm_hist_frac(), accent, 0);
        nxc_restore(c);
    } else if (bar >= 0.0f) {
        nxc_rect_t b = nxc_rect(r.x + r.w * 0.56f, r.y + 33.0f, r.w * 0.44f - 12.0f, 6.0f);
        nxc_fill_rrect(c, b, 3.0f, th->control_fill_hover);
        if (bar > 0.0f) nxc_fill_rrect(c, nxc_rect(b.x, b.y, b.w * (bar > 1.0f ? 1.0f : bar), b.h), 3.0f, accent);
    }
}

static void tm_draw_tiles(nxc_canvas_t *c, nxc_rect_t r) {
    const nxproc_sys_stats_t *s = &g_tm.sys;
    int ok = (g_tm.sys_st == NXPROC_OK);
    float gap = 10.0f, w = (r.w - 3.0f * gap) / 4.0f;
    char v[48], sub[64];
    tm_sb_t b, sb;

    /* CPU */
    sb_init(&b, v, sizeof(v)); sb_init(&sb, sub, sizeof(sub));
    if (ok && (s->valid & NXPROC_SYS_CPU)) {
        sb_x10(&b, s->cpu_pct_x10); sb_str(&b, "%");
        sb_u64(&sb, s->cpu_count); sb_str(&sb, s->cpu_count == 1 ? " core" : " cores");
    } else { sb_str(&b, "\xE2\x80\x94"); sb_str(&sb, "Not reported by kernel"); }
    tm_tile(c, nxc_rect(r.x, r.y, w, r.h), "CPU", v, sub, tm_cpu_color(), -1.0f,
            (ok && (s->valid & NXPROC_SYS_CPU)) ? g_tm.cpu_hist : 0, g_tm.cpu_n);

    /* Memory */
    sb_init(&b, v, sizeof(v)); sb_init(&sb, sub, sizeof(sub));
    float mem = -1.0f;
    if (ok && (s->valid & NXPROC_SYS_MEMORY)) {
        sb_bytes(&b, s->mem_used);
        sb_str(&sb, "of "); sb_bytes(&sb, s->mem_total); sb_str(&sb, " in use");
        mem = (float)((double)s->mem_used / (double)s->mem_total);
    } else { sb_str(&b, "\xE2\x80\x94"); sb_str(&sb, "Not reported by kernel"); }
    tm_tile(c, nxc_rect(r.x + (w + gap), r.y, w, r.h), "Memory", v, sub, tm_mem_color(), mem, 0, 0);

    /* Processes */
    sb_init(&b, v, sizeof(v)); sb_init(&sb, sub, sizeof(sub));
    if (g_tm.snap_st == NXPROC_OK || g_tm.snap_st == NXPROC_ERR_TRUNCATED) {
        sb_u64(&b, g_tm.snap.kernel_count);
        if (ok && (s->valid & NXPROC_SYS_PROCS)) { sb_u64(&sb, s->thread_count); sb_str(&sb, " threads"); }
    } else { sb_str(&b, "\xE2\x80\x94"); sb_str(&sb, "Process list unavailable"); }
    tm_tile(c, nxc_rect(r.x + 2.0f * (w + gap), r.y, w, r.h), "Processes", v, sub,
            nxui_theme()->success, -1.0f, 0, 0);

    /* Uptime */
    sb_init(&b, v, sizeof(v)); sb_init(&sb, sub, sizeof(sub));
    if (ok && (s->valid & NXPROC_SYS_UPTIME)) { sb_duration(&b, s->uptime_ms); sb_str(&sb, "Since last start"); }
    else { sb_str(&b, "\xE2\x80\x94"); sb_str(&sb, "Not reported by kernel"); }
    tm_tile(c, nxc_rect(r.x + 3.0f * (w + gap), r.y, w, r.h), "Uptime", v, sub,
            nxui_theme()->warning, -1.0f, 0, 0);
}

/* ============ Tabs ============ */

static void tm_draw_processes(nxc_canvas_t *c, const tm_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    tm_draw_tiles(c, nxc_rect(L->body.x, L->body.y, L->body.w, 58.0f));

    if (g_tm.snap_st != NXPROC_OK && g_tm.snap_st != NXPROC_ERR_TRUNCATED) {
        nxc_rect_t r = nxc_rect(L->body.x, L->body.y + 70.0f, L->body.w, L->body.h - 74.0f);
        nxglass_card(c, r, 12.0f);
        if (g_tm.snap_st == NXPROC_ERR_UNSUPPORTED)
            tm_unavailable(c, r, "Process information unavailable",
                "This NeolyxOS kernel does not yet let apps read its process table, so no "
                "processes can be listed or ended here. Open windows are listed under Apps; "
                "memory and uptime are under Performance.");
        else
            tm_unavailable(c, r, "Couldn't read processes", nxproc_status_str(g_tm.snap_st));
        return;
    }

    tm_table_t T = tm_table(L, k_pcols, PC_COUNT, tm_proc_table_top(L));
    tm_header(c, &T, k_pcols, g_tm.sort_col, g_tm.sort_desc, 1);

    if (g_tm.nvis == 0) {
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(T.rows.x, T.rows.y + 20.0f, T.rows.w, 20.0f),
                          g_tm.search_len ? "No processes match your search" : "No processes",
                          th->text_secondary, NXC_ALIGN_CENTER);
        return;
    }
    int maxs = g_tm.nvis - T.visible_rows;
    if (maxs < 0) maxs = 0;
    if (g_tm.scroll > maxs) g_tm.scroll = maxs;
    if (g_tm.scroll < 0) g_tm.scroll = 0;

    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)T.rows.x, (int32_t)T.rows.y, (int32_t)T.rows.w, (int32_t)T.rows.h));
    char buf[48];
    tm_sb_t b;
    for (int k = 0; k < T.visible_rows + 1 && g_tm.scroll + k < g_tm.nvis; k++) {
        const nxproc_info_t *p = &g_tm.snap.procs[g_tm.order[g_tm.scroll + k]];
        float y = T.rows.y + (float)k * TM_ROW_H;
        nxc_rect_t rr = nxc_rect(T.rows.x, y + 1.0f, T.rows.w - 10.0f, TM_ROW_H - 2.0f);
        int sel = (p->pid == g_tm.sel_pid);
        int hov = g_tm.pointer_in && !g_tm.sheet_open && !g_tm.prio_open && nxglass_hit(rr, g_tm.mx, g_tm.my);
        tm_row_bg(c, rr, sel, hov);
        int sys = (p->flags & (NXPROC_F_SYSTEM | NXPROC_F_KERNEL)) != 0;
        nxc_color_t tc = th->text_primary, dim = th->text_secondary;
        if (sel) { tc = th->text_on_accent; dim = NXC_WITH_ALPHA(th->text_on_accent, 215); }

        /* CPU heat behind the CPU cell */
        if (p->cpu_pct_x10 >= 30 && !sel) {
            float a = 18.0f + (float)p->cpu_pct_x10 * 0.35f;
            if (a > 110.0f) a = 110.0f;
            nxc_fill_rrect(c, nxc_rect(T.colx[PC_CPU] + 2.0f, y + 3.0f, T.colw[PC_CPU] - 4.0f, TM_ROW_H - 6.0f),
                           5.0f, NXC_WITH_ALPHA(tm_cpu_color(), (uint8_t)a));
        }
        tm_initial_icon(c, T.colx[PC_NAME] + 6.0f, y + TM_ROW_H * 0.5f, 16.0f, p->name, sys);
        nxc_rect_t nr = nxc_rect(T.colx[PC_NAME] + 30.0f, y, T.colw[PC_NAME] - 36.0f, TM_ROW_H);
        if (p->flags & NXPROC_F_SELF) {
            nxui_text_in_rect(c, NXUI_TEXT_BODY, nr, p->name, tc, NXC_ALIGN_LEFT);
            float tw = nxui_text_width(NXUI_TEXT_BODY, p->name);
            nxglass_badge(c, nr.x + tw + 8.0f, y + 4.0f, "This app", sel ? th->text_on_accent : th->accent);
        } else {
            nxui_text_in_rect(c, NXUI_TEXT_BODY, nr, p->name, tc, NXC_ALIGN_LEFT);
        }
        sb_init(&b, buf, sizeof(buf)); sb_u64(&b, p->pid);
        tm_cell(c, &T, k_pcols, PC_PID, y, buf, dim);
        tm_cell(c, &T, k_pcols, PC_STATE, y, nxproc_state_name(p->state),
                (p->state == NXPROC_STATE_ZOMBIE && !sel) ? th->warning : dim);
        tm_cell(c, &T, k_pcols, PC_OWNER, y, p->owner, dim);
        sb_init(&b, buf, sizeof(buf)); sb_u64(&b, p->threads);
        tm_cell(c, &T, k_pcols, PC_THREADS, y, buf, dim);
        sb_init(&b, buf, sizeof(buf)); sb_x10(&b, p->cpu_pct_x10); sb_str(&b, "%");
        tm_cell(c, &T, k_pcols, PC_CPU, y, buf, tc);
        sb_init(&b, buf, sizeof(buf));
        if (p->resident_bytes) sb_bytes(&b, p->resident_bytes); else sb_str(&b, "\xE2\x80\x94");
        tm_cell(c, &T, k_pcols, PC_MEM, y, buf, tc);
    }
    nxc_restore(c);
    tm_scrollbar(c, T.rows, g_tm.nvis, T.visible_rows, g_tm.scroll);
}

static void tm_graph_card(nxc_canvas_t *c, nxc_rect_t r, const char *title, const char *value,
                          const char *sub, const float *hist, int hn, int valid, nxc_color_t col,
                          const char *missing) {
    const nxui_theme_t *th = nxui_theme();
    nxglass_card(c, r, 12.0f);
    nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(r.x + 16.0f, r.y + 12.0f, 200.0f, 18.0f),
                      title, th->text_primary, NXC_ALIGN_LEFT);
    if (sub)
        nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(r.x + 16.0f, r.y + 30.0f, r.w * 0.6f, 16.0f),
                          sub, th->text_secondary, NXC_ALIGN_LEFT);
    if (value)
        nxui_text_in_rect(c, NXUI_TEXT_TITLE1, nxc_rect(r.x + r.w - 196.0f, r.y + 12.0f, 180.0f, 30.0f),
                          value, valid ? col : th->text_tertiary, NXC_ALIGN_RIGHT);
    nxc_rect_t g = nxc_rect(r.x + 16.0f, r.y + 54.0f, r.w - 32.0f, r.h - 54.0f - 26.0f);
    nxc_fill_rrect(c, g, 6.0f, nxui_appearance() == NXUI_APPEARANCE_LIGHT
                                   ? NXC_ARGB(10, 0, 0, 0) : NXC_ARGB(46, 0, 0, 0));
    nxc_stroke_rrect(c, g, 6.0f, 1.0f, th->separator);
    if (valid) {
        nxc_save(c);
        nxc_clip_rrect(c, g, 6.0f);
        tm_area_chart(c, g, hist, hn, tm_hist_frac(), col, 1);
        nxc_restore(c);
    } else {
        for (int k = 1; k < 4; k++)
            nxc_fill_rect(c, nxc_rect(g.x, g.y + g.h * (float)k / 4.0f, g.w, 1.0f), tm_fade(th->separator, 0.7f));
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, g, missing, th->text_secondary, NXC_ALIGN_CENTER);
    }
    float ly = g.y + g.h + 4.0f;
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(g.x, ly, 80.0f, 16.0f), "60 seconds", th->text_tertiary, NXC_ALIGN_LEFT);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(g.x + g.w - 80.0f, ly, 80.0f, 16.0f), "Now", th->text_tertiary, NXC_ALIGN_RIGHT);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(g.x + g.w - 46.0f, g.y + 2.0f, 40.0f, 14.0f), "100%",
                      th->text_tertiary, NXC_ALIGN_RIGHT);
}

static void tm_detail_row(nxc_canvas_t *c, float x, float y, float w, const char *k, const char *v) {
    const nxui_theme_t *th = nxui_theme();
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(x, y, w * 0.5f, 22.0f), k, th->text_secondary, NXC_ALIGN_LEFT);
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(x + w * 0.4f, y, w * 0.6f, 22.0f), v, th->text_primary, NXC_ALIGN_RIGHT);
}

static void tm_draw_performance(nxc_canvas_t *c, const tm_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    const nxproc_sys_stats_t *s = &g_tm.sys;
    int ok = (g_tm.sys_st == NXPROC_OK);
    int cpu_ok = ok && (s->valid & NXPROC_SYS_CPU);
    int mem_ok = ok && (s->valid & NXPROC_SYS_MEMORY);
    float side = 224.0f, gap = 12.0f;
    float gw = L->body.w - side - gap;
    float gh = (L->body.h - gap - 4.0f) * 0.5f;
    char v[40], sub[80];
    tm_sb_t b, sb;

    sb_init(&b, v, sizeof(v)); sb_init(&sb, sub, sizeof(sub));
    if (cpu_ok) { sb_x10(&b, s->cpu_pct_x10); sb_str(&b, "%"); }
    else sb_str(&b, "\xE2\x80\x94");
    if (ok && s->cpu_count) {
        sb_str(&sb, "Utilisation across ");
        sb_u64(&sb, s->cpu_count);
        sb_str(&sb, s->cpu_count == 1 ? " core" : " cores");
    } else {
        sb_str(&sb, "Processor utilisation");
    }
    tm_graph_card(c, nxc_rect(L->body.x, L->body.y, gw, gh), "CPU", v, sub, g_tm.cpu_hist, g_tm.cpu_n,
                  cpu_ok && g_tm.cpu_n >= 2, tm_cpu_color(),
                  cpu_ok ? "Collecting\xE2\x80\xA6" : "CPU load is not reported by this kernel yet");

    sb_init(&b, v, sizeof(v)); sb_init(&sb, sub, sizeof(sub));
    if (mem_ok) {
        sb_bytes(&b, s->mem_used);
        sb_str(&sb, "In use of "); sb_bytes(&sb, s->mem_total);
    } else { sb_str(&b, "\xE2\x80\x94"); sb_str(&sb, "Physical memory"); }
    tm_graph_card(c, nxc_rect(L->body.x, L->body.y + gh + gap, gw, gh), "Memory", v, sub,
                  g_tm.mem_hist, g_tm.mem_n, mem_ok && g_tm.mem_n >= 2, tm_mem_color(),
                  mem_ok ? "Collecting\xE2\x80\xA6" : "Memory figures are not available");

    /* Cores */
    float sx = L->body.x + gw + gap;
    nxc_rect_t cr = nxc_rect(sx, L->body.y, side, 166.0f);
    nxglass_card(c, cr, 12.0f);
    nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(cr.x + 14.0f, cr.y + 10.0f, 120.0f, 18.0f),
                      "Cores", th->text_primary, NXC_ALIGN_LEFT);
    if (ok && (s->valid & NXPROC_SYS_PER_CORE) && s->cpu_count) {
        uint32_t n = s->cpu_count > NXPROC_MAX_CPUS ? NXPROC_MAX_CPUS : s->cpu_count;
        float area_w = cr.w - 28.0f, bw = area_w / (float)n;
        float bh = cr.h - 66.0f, by = cr.y + 38.0f;
        for (uint32_t i = 0; i < n; i++) {
            float bx = cr.x + 14.0f + bw * (float)i + bw * 0.18f, w = bw * 0.64f;
            float v01 = (float)s->core_pct_x10[i] / 1000.0f;
            nxc_fill_rrect(c, nxc_rect(bx, by, w, bh), 3.0f, th->control_fill);
            float fh = bh * v01;
            if (fh > 1.0f) {
                nxc_paint_t p = nxc_paint_linear(0.0f, by + bh - fh, 0.0f, by + bh,
                                                 tm_cpu_color(), NXC_WITH_ALPHA(tm_cpu_color(), 150));
                nxc_fill_rrect_paint(c, nxc_rect(bx, by + bh - fh, w, fh), 3.0f, &p);
            }
            char lbl[8]; tm_sb_t lb; sb_init(&lb, lbl, sizeof(lbl)); sb_u64(&lb, i + 1u);
            nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(bx - 4.0f, by + bh + 4.0f, w + 8.0f, 14.0f),
                              lbl, th->text_tertiary, NXC_ALIGN_CENTER);
        }
    } else {
        tm_wrap(c, NXUI_TEXT_FOOTNOTE, cr.x + 14.0f, cr.y + 40.0f, cr.w - 28.0f, 18.0f,
                "Per-core load is not reported by this kernel yet.", th->text_secondary, 3);
    }

    /* Details */
    nxc_rect_t dr = nxc_rect(sx, cr.y + cr.h + gap, side, L->body.h - cr.h - gap - 4.0f);
    nxglass_card(c, dr, 12.0f);
    nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(dr.x + 14.0f, dr.y + 10.0f, 120.0f, 18.0f),
                      "System", th->text_primary, NXC_ALIGN_LEFT);
    float x = dr.x + 14.0f, w = dr.w - 28.0f, y = dr.y + 34.0f;
    const char *na = "\xE2\x80\x94";
    char t[40];
    sb_init(&b, t, sizeof(t)); if (ok && (s->valid & NXPROC_SYS_UPTIME)) sb_duration(&b, s->uptime_ms); else sb_str(&b, na);
    tm_detail_row(c, x, y, w, "Uptime", t); y += 22.0f;
    sb_init(&b, t, sizeof(t)); if (ok && (s->valid & NXPROC_SYS_PROCS)) sb_u64(&b, s->proc_count); else sb_str(&b, na);
    tm_detail_row(c, x, y, w, "Processes", t); y += 22.0f;
    sb_init(&b, t, sizeof(t)); if (ok && (s->valid & NXPROC_SYS_PROCS)) sb_u64(&b, s->thread_count); else sb_str(&b, na);
    tm_detail_row(c, x, y, w, "Threads", t); y += 22.0f;
    nxglass_separator(c, x, y + 4.0f, w); y += 10.0f;
    sb_init(&b, t, sizeof(t)); if (mem_ok) sb_bytes(&b, s->mem_total); else sb_str(&b, na);
    tm_detail_row(c, x, y, w, "Memory", t); y += 22.0f;
    sb_init(&b, t, sizeof(t)); if (mem_ok) sb_bytes(&b, s->mem_used); else sb_str(&b, na);
    tm_detail_row(c, x, y, w, "Used", t); y += 22.0f;
    sb_init(&b, t, sizeof(t)); if (mem_ok) sb_bytes(&b, s->mem_free); else sb_str(&b, na);
    tm_detail_row(c, x, y, w, "Free", t); y += 22.0f;
    if (y + 22.0f <= dr.y + dr.h) {
        sb_init(&b, t, sizeof(t)); if (ok && s->cpu_count) sb_u64(&b, s->cpu_count); else sb_str(&b, na);
        tm_detail_row(c, x, y, w, "Processors", t);
    }
}

static void tm_draw_apps(nxc_canvas_t *c, const tm_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    uint32_t ids[MAX_WINDOWS];
    int n = tm_apps(ids, MAX_WINDOWS);
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(L->body.x + 2.0f, L->body.y, L->body.w, 20.0f),
                      "Windows open on this desktop. End Task closes the window; its app runs inside the desktop process.",
                      th->text_secondary, NXC_ALIGN_LEFT);
    tm_table_t T = tm_table(L, k_acols, AC_COUNT, L->body.y + 28.0f);
    tm_header(c, &T, k_acols, -1, 0, 0);
    if (n == 0) {
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(T.rows.x, T.rows.y + 20.0f, T.rows.w, 20.0f),
                          g_tm.search_len ? "No windows match your search" : "No windows are open",
                          th->text_secondary, NXC_ALIGN_CENTER);
        return;
    }
    uint32_t focused = desktop_focused_window_id();
    int maxs = n - T.visible_rows;
    if (g_tm.app_scroll > (maxs > 0 ? maxs : 0)) g_tm.app_scroll = maxs > 0 ? maxs : 0;
    if (g_tm.app_scroll < 0) g_tm.app_scroll = 0;
    char buf[48];
    tm_sb_t b;
    for (int k = 0; k < T.visible_rows && g_tm.app_scroll + k < n; k++) {
        const desktop_window_t *w = tm_window(ids[g_tm.app_scroll + k]);
        if (!w) continue;
        float y = T.rows.y + (float)k * TM_ROW_H;
        nxc_rect_t rr = nxc_rect(T.rows.x, y + 1.0f, T.rows.w - 10.0f, TM_ROW_H - 2.0f);
        int sel = (w->id == g_tm.sel_win);
        tm_row_bg(c, rr, sel, g_tm.pointer_in && !g_tm.sheet_open && nxglass_hit(rr, g_tm.mx, g_tm.my));
        tm_initial_icon(c, T.colx[AC_NAME] + 6.0f, y + TM_ROW_H * 0.5f, 16.0f, w->title, 0);
        nxc_rect_t nr = nxc_rect(T.colx[AC_NAME] + 30.0f, y, T.colw[AC_NAME] - 36.0f, TM_ROW_H);
        nxc_color_t tc = sel ? th->text_on_accent : th->text_primary;
        nxc_color_t dim = sel ? NXC_WITH_ALPHA(th->text_on_accent, 215) : th->text_secondary;
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nr, w->title, tc, NXC_ALIGN_LEFT);
        if (w->id == g_tm.win_id) {
            float tw = nxui_text_width(NXUI_TEXT_BODY, w->title);
            nxglass_badge(c, nr.x + tw + 8.0f, y + 4.0f, "This app", sel ? th->text_on_accent : th->accent);
        }
        const char *stt = w->minimized ? "Minimized" : (w->id == focused ? "Active" : "Running");
        tm_cell(c, &T, k_acols, AC_STATUS, y, stt, (w->minimized && !sel) ? th->text_tertiary : dim);
        sb_init(&b, buf, sizeof(buf)); sb_u64(&b, w->width); sb_str(&b, " \xC3\x97 "); sb_u64(&b, w->height);
        tm_cell(c, &T, k_acols, AC_SIZE, y, buf, dim);
        sb_init(&b, buf, sizeof(buf)); sb_u64(&b, w->id);
        tm_cell(c, &T, k_acols, AC_ID, y, buf, dim);
    }
    tm_scrollbar(c, T.rows, n, T.visible_rows, g_tm.app_scroll);
}

static void tm_draw_startup(nxc_canvas_t *c, const tm_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    if (g_tm.startup_st != NXPROC_OK) {
        nxglass_card(c, nxc_rect(L->body.x, L->body.y, L->body.w, L->body.h - 4.0f), 12.0f);
        tm_unavailable(c, L->body, "Startup items unavailable",
            g_tm.startup_st == NXPROC_ERR_UNSUPPORTED
                ? "This NeolyxOS build has no registry of login items that apps can read yet. "
                  "The desktop still opens its built-in startup apps."
                : nxproc_status_str(g_tm.startup_st));
        return;
    }
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(L->body.x + 2.0f, L->body.y, L->body.w, 20.0f),
                      "Apps and services that start when you log in. Read-only for now.",
                      th->text_secondary, NXC_ALIGN_LEFT);
    tm_table_t T = tm_table(L, k_scols, SC_COUNT, L->body.y + 28.0f);
    tm_header(c, &T, k_scols, -1, 0, 0);
    static const char *const impact[] = { "Not measured", "Low", "Medium", "High" };
    int row = 0;
    for (uint32_t i = 0; i < g_tm.startup_n && row < T.visible_rows; i++) {
        const nxproc_startup_t *s = &g_tm.startup[i];
        if (g_tm.search_len && !tm_contains_ci(s->name, g_tm.search)) continue;
        float y = T.rows.y + (float)row * TM_ROW_H;
        nxc_rect_t rr = nxc_rect(T.rows.x, y + 1.0f, T.rows.w - 10.0f, TM_ROW_H - 2.0f);
        tm_row_bg(c, rr, (int)i == g_tm.sel_start, g_tm.pointer_in && nxglass_hit(rr, g_tm.mx, g_tm.my));
        int svc = (s->path[0] == '/' && s->path[1] == 'S');
        tm_initial_icon(c, T.colx[SC_NAME] + 6.0f, y + TM_ROW_H * 0.5f, 16.0f, s->name, svc);
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(T.colx[SC_NAME] + 30.0f, y, T.colw[SC_NAME] - 36.0f, TM_ROW_H),
                          s->name, th->text_primary, NXC_ALIGN_LEFT);
        tm_cell(c, &T, k_scols, SC_PATH, y, s->path, th->text_secondary);
        nxglass_badge(c, T.colx[SC_STATUS] + 6.0f, y + 4.0f, s->enabled ? "Enabled" : "Disabled",
                      s->enabled ? th->success : th->text_tertiary);
        uint8_t im = s->impact > 3 ? 0 : s->impact;
        tm_cell(c, &T, k_scols, SC_IMPACT, y, impact[im],
                im == 3 ? th->warning : th->text_secondary);
        row++;
    }
}

/* ============ Footer, popover and sheet ============ */

static uint32_t tm_btn_state(nxc_rect_t r, int enabled, int id) {
    uint32_t st = 0;
    if (!enabled) return NXGLASS_DISABLED;
    if (g_tm.pointer_in && !g_tm.sheet_open && nxglass_hit(r, g_tm.mx, g_tm.my)) {
        st |= NXGLASS_HOVER;
        if (g_tm.pressed_btn == id) st |= NXGLASS_PRESSED;
    }
    return st;
}

static void tm_draw_footer(nxc_canvas_t *c, const tm_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    nxframe_strip(c, L->footer, NULL);   /* Neolyx status strip */
    nxc_rect_t tr = nxc_rect(L->footer.x + TM_PAD, L->footer.y, L->footer.w - 2.0f * TM_PAD - 240.0f, L->footer.h);
    char msg[128];
    tm_sb_t b; sb_init(&b, msg, sizeof(msg));
    nxc_color_t col = th->text_secondary;
    if (g_tm.status[0] && nx_gettime() < g_tm.status_until_ms) {
        sb_str(&b, g_tm.status);
        col = g_tm.status_err ? th->destructive : th->text_primary;
    } else if (g_tm.tab == TAB_PROC) {
        const nxproc_info_t *p = tm_selected();
        if (p) {
            sb_str(&b, p->name); sb_str(&b, "  \xC2\xB7  PID "); sb_u64(&b, p->pid);
            sb_str(&b, "  \xC2\xB7  "); sb_str(&b, nxproc_priority_name(p->priority));
            sb_str(&b, " priority  \xC2\xB7  CPU time ");
            sb_duration(&b, p->cpu_time_ms);
        } else if (g_tm.snap_st == NXPROC_OK || g_tm.snap_st == NXPROC_ERR_TRUNCATED) {
            sb_u64(&b, (uint64_t)g_tm.nvis); sb_str(&b, " of "); sb_u64(&b, g_tm.snap.kernel_count);
            sb_str(&b, " processes");
            if (g_tm.snap_st == NXPROC_ERR_TRUNCATED) sb_str(&b, " (list truncated)");
            if (g_tm.caps & NXPROC_CAP_DEMO) sb_str(&b, "  \xC2\xB7  Preview demo data");
        } else {
            sb_str(&b, "Updates every second");
        }
    } else if (g_tm.tab == TAB_PERF) {
        sb_str(&b, "Updates every second");
        if (g_tm.caps & NXPROC_CAP_DEMO) sb_str(&b, "  \xC2\xB7  Preview demo data");
    } else if (g_tm.tab == TAB_APPS) {
        const desktop_window_t *w = g_tm.sel_win ? tm_window(g_tm.sel_win) : 0;
        if (w) { sb_str(&b, w->title); sb_str(&b, "  \xC2\xB7  window "); sb_u64(&b, w->id); }
        else sb_str(&b, "Select a window to switch to it or end it");
    } else {
        sb_str(&b, "Startup items can't be changed here yet");
    }
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, tr, msg, col, NXC_ALIGN_LEFT);

    if (g_tm.tab == TAB_PROC) {
        const nxproc_info_t *p = tm_selected();
        int prio_ok = p && (g_tm.caps & NXPROC_CAP_PRIORITY) && !(p->flags & (NXPROC_F_KERNEL | NXPROC_F_SYSTEM));
        nxc_rect_t pb = tm_footer_btn(L, 2), eb = tm_footer_btn(L, 1);
        nxglass_button(c, pb, "Priority", NXGLASS_BUTTON_DEFAULT,
                       tm_btn_state(pb, prio_ok, 2) | (g_tm.prio_open ? NXGLASS_PRESSED : 0));
        nxsym_draw(c, NXSYM_CHEVRON_UP, pb.x + pb.w - 14.0f, pb.y + pb.h * 0.5f, 9.0f,
                   prio_ok ? th->text_secondary : th->text_tertiary, 0.0f);
        nxglass_button(c, eb, "End Task", NXGLASS_BUTTON_DEFAULT, tm_btn_state(eb, tm_proc_endable(p), 1));
    } else if (g_tm.tab == TAB_APPS) {
        int have = g_tm.sel_win && tm_window(g_tm.sel_win);
        nxc_rect_t sbtn = tm_footer_btn(L, 2), eb = tm_footer_btn(L, 1);
        nxglass_button(c, sbtn, "Switch To", NXGLASS_BUTTON_DEFAULT, tm_btn_state(sbtn, have, 3));
        nxglass_button(c, eb, "End Task", NXGLASS_BUTTON_DEFAULT, tm_btn_state(eb, have, 1));
    }
}

static void tm_draw_prio(nxc_canvas_t *c, const tm_layout_t *L) {
    const nxproc_info_t *p = tm_selected();
    if (!g_tm.prio_open || !p) return;
    nxc_rect_t r = tm_prio_rect(L);
    nxglass_panel(c, r, 10.0f, NXGLASS_THIN, 2, 1);
    for (int i = 0; i < TM_NPRIO; i++) {
        nxc_rect_t rr = nxc_rect(r.x + 6.0f, r.y + 6.0f + 28.0f * (float)i, r.w - 12.0f, 28.0f);
        int cur = (nxproc_priority_name(p->priority) == nxproc_priority_name(k_prios[i].value));
        uint32_t st = (g_tm.pointer_in && nxglass_hit(rr, g_tm.mx, g_tm.my)) ? NXGLASS_HOVER : 0;
        nxglass_row(c, rr, cur ? (int)NXSYM_CHECK : (int)NXSYM_COUNT, k_prios[i].name, 0, st);
    }
}

static void tm_draw_sheet(nxc_canvas_t *c, const tm_layout_t *L) {
    if (g_tm.sheet_t <= 0.01f) return;
    const nxui_theme_t *th = nxui_theme();
    float t = g_tm.sheet_t;
    nxc_fill_rect(c, L->content, NXC_ARGB((uint8_t)(t * 90.0f), 0, 0, 0));
    nxc_rect_t s = tm_sheet_rect(L);
    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(t * 255.0f));
    nxglass_panel(c, s, 14.0f, NXGLASS_REGULAR, 3, 1);
    nxc_fill_rrect(c, s, 14.0f, NXC_WITH_ALPHA(th->window_bg, 150));
    nxc_stroke_rrect(c, s, 14.0f, 1.0f, th->window_border);

    float ix = s.x + 42.0f, iy = s.y + 44.0f;
    nxc_fill_circle(c, ix, iy, 21.0f, NXC_WITH_ALPHA(th->destructive, 46));
    nxsym_draw(c, NXSYM_WARNING, ix, iy, 22.0f, th->destructive, 0.0f);

    char title[96];
    tm_sb_t b; sb_init(&b, title, sizeof(title));
    sb_str(&b, "End \xE2\x80\x9C"); sb_str(&b, g_tm.sheet_name); sb_str(&b, "\xE2\x80\x9D?");
    float tx = s.x + 78.0f, tw = s.w - 98.0f;
    nxui_text_in_rect(c, NXUI_TEXT_TITLE3, nxc_rect(tx, s.y + 22.0f, tw, 22.0f), title, th->text_primary, NXC_ALIGN_LEFT);

    char body[200];
    sb_init(&b, body, sizeof(body));
    if (g_tm.sheet_is_app) {
        sb_str(&b, "The window will close. Any unsaved changes in it will be lost.");
    } else {
        sb_str(&b, "PID "); sb_u64(&b, g_tm.sheet_pid);
        sb_str(&b, " will be asked to quit. If it doesn't respond within ");
        sb_u64(&b, NXPROC_GRACE_MS / 1000u);
        sb_str(&b, " seconds it is forced to close, and unsaved work is lost.");
    }
    tm_wrap(c, NXUI_TEXT_FOOTNOTE, tx, s.y + 50.0f, tw, 18.0f, body, th->text_secondary, 3);

    nxc_rect_t cb = tm_sheet_btn(s, 1), ok = tm_sheet_btn(s, 2);
    nxglass_button(c, cb, "Cancel", NXGLASS_BUTTON_DEFAULT, g_tm.sheet_hover == 1 ? NXGLASS_HOVER : 0);
    nxglass_button(c, ok, "End Task", NXGLASS_BUTTON_DESTRUCTIVE,
                   NXGLASS_FOCUSED | (g_tm.sheet_hover == 2 ? NXGLASS_HOVER : 0));
    nxc_restore(c);
}

/* ============ Actions ============ */

static void tm_open_sheet(void) {
    if (g_tm.tab == TAB_PROC) {
        const nxproc_info_t *p = tm_selected();
        if (!p) return;
        if (!tm_proc_endable(p)) {
            char msg[112];
            tm_sb_t b; sb_init(&b, msg, sizeof(msg));
            if (!(g_tm.caps & NXPROC_CAP_END)) sb_str(&b, "Ending processes isn't supported by this kernel yet.");
            else { sb_str(&b, p->name); sb_str(&b, " is protected and can't be ended."); }
            tm_set_status(msg, 1);
            return;
        }
        g_tm.sheet_is_app = 0;
        g_tm.sheet_pid = p->pid;
        tm_copy(g_tm.sheet_name, sizeof(g_tm.sheet_name), p->name);
    } else if (g_tm.tab == TAB_APPS) {
        const desktop_window_t *w = g_tm.sel_win ? tm_window(g_tm.sel_win) : 0;
        if (!w) return;
        g_tm.sheet_is_app = 1;
        g_tm.sheet_win = w->id;
        tm_copy(g_tm.sheet_name, sizeof(g_tm.sheet_name), w->title);
    } else {
        return;
    }
    g_tm.prio_open = 0;
    g_tm.sheet_open = 1;
    g_tm.sheet_hover = 0;
}

static void tm_confirm_sheet(void) {
    char msg[112];
    tm_sb_t b; sb_init(&b, msg, sizeof(msg));
    g_tm.sheet_open = 0;
    if (g_tm.sheet_is_app) {
        uint32_t id = g_tm.sheet_win;
        if (desktop_close_window(id) == 0) {
            sb_str(&b, "Closed "); sb_str(&b, g_tm.sheet_name); sb_str(&b, ".");
            tm_set_status(msg, 0);
            tm_log("closed an app window (End Task)");
            if (g_tm.sel_win == id) g_tm.sel_win = 0;
        } else {
            sb_str(&b, g_tm.sheet_name); sb_str(&b, " is no longer open.");
            tm_set_status(msg, 1);
        }
        return;
    }
    nxproc_status_t st = nxproc_end(g_tm.sheet_pid, NXPROC_END_GRACEFUL, NXPROC_GRACE_MS);
    if (st == NXPROC_OK) {
        g_tm.pending_pid = g_tm.sheet_pid;
        g_tm.pending_deadline_ms = nx_gettime() + NXPROC_GRACE_MS;
        tm_copy(g_tm.pending_name, sizeof(g_tm.pending_name), g_tm.sheet_name);
        sb_str(&b, "Asked "); sb_str(&b, g_tm.sheet_name); sb_str(&b, " to quit\xE2\x80\xA6");
        tm_set_status(msg, 0);
        g_tm.next_refresh_ms = nx_gettime() + 300u;   /* look again soon */
    } else {
        sb_str(&b, "Couldn't end "); sb_str(&b, g_tm.sheet_name); sb_str(&b, ": ");
        sb_str(&b, nxproc_status_str(st));
        tm_set_status(msg, 1);
    }
}

static void tm_set_prio(int idx) {
    const nxproc_info_t *p = tm_selected();
    g_tm.prio_open = 0;
    if (!p || idx < 0 || idx >= TM_NPRIO) return;
    nxproc_status_t st = nxproc_set_priority(p->pid, k_prios[idx].value);
    char msg[112];
    tm_sb_t b; sb_init(&b, msg, sizeof(msg));
    if (st == NXPROC_OK) {
        sb_str(&b, p->name); sb_str(&b, " priority set to "); sb_str(&b, k_prios[idx].name); sb_str(&b, ".");
        tm_set_status(msg, 0);
        g_tm.next_refresh_ms = nx_gettime();
    } else {
        sb_str(&b, "Couldn't change priority: "); sb_str(&b, nxproc_status_str(st));
        tm_set_status(msg, 1);
    }
}

static void tm_set_tab(int tab) {
    if (tab < 0 || tab >= TAB_COUNT || tab == g_tm.tab) return;
    g_tm.tab = tab;
    g_tm.prio_open = 0;
    g_tm.search_focus = 0;
}

static void tm_search_changed(void) {
    g_tm.scroll = 0;
    g_tm.app_scroll = 0;
    tm_rebuild_order();
}

/* ============ View entry points ============ */

int taskmgr_view_open(uint32_t win_id) {
    if (win_id == 0) return -1;
    /* Fresh state per window (static storage; no allocation). */
    uint8_t *raw = (uint8_t *)&g_tm;
    for (uint32_t i = 0; i < sizeof(g_tm); i++) raw[i] = 0;
    g_tm.win_id = win_id;
    g_tm.hover_tab = -1;
    g_tm.sort_col = PC_CPU;
    g_tm.sort_desc = 1;
    g_tm.sel_start = -1;
    g_tm.caps = nxproc_capabilities();
    if (nxproc_sampler_init(&g_tm.sampler) != NXPROC_OK) {
        tm_log("sampler init failed");
        return -1;
    }
    uint32_t ppid = 0;
    if (nxproc_self(&g_tm.self_pid, &ppid) != NXPROC_OK) g_tm.self_pid = 0;
    g_tm.startup_st = nxproc_startup_list(g_tm.startup, NXPROC_MAX_STARTUP, &g_tm.startup_n);
#if defined(NX_HOST_PREVIEW) && !defined(NXPROC_NO_DEMO)
    /* Preview only: fast-forward demo time so the graphs open with a full
     * minute of history instead of filling up over 60 real seconds. */
    for (int i = 0; i < TM_HIST_N - 1; i++) {
        nxproc_demo_advance(TM_REFRESH_MS);
        tm_sample();
    }
#endif
    tm_sample();
    tm_log("opened");
    return 0;
}

void taskmgr_view_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    if (!win) return;
    nxc_canvas_t *c = nxui_canvas();
    if (!c) return;
    if (nx_gettime() >= g_tm.next_refresh_ms) tm_sample();

    float dt = nxui_frame_dt();
    g_tm.sheet_t = nxui_approach(g_tm.sheet_t, g_tm.sheet_open ? 1.0f : 0.0f, 0.06f, dt);

    tm_layout_t L = tm_layout(win);
    if (g_tm.tab >= 0 && g_tm.tab < TAB_COUNT) desktop_set_window_place(win->id, k_tabs[g_tm.tab]);
    g_tm.hover_tab = (g_tm.pointer_in && !g_tm.sheet_open)
                   ? nxglass_segment_at(L.tabs, TAB_COUNT, g_tm.mx, g_tm.my) : -1;
    nxglass_segmented(c, L.tabs, k_tabs, TAB_COUNT, g_tm.tab, g_tm.hover_tab);
    if (tm_has_search()) {
        const char *ph = g_tm.tab == TAB_APPS ? "Search windows"
                       : (g_tm.tab == TAB_START ? "Search startup items" : "Search name or PID");
        nxglass_field(c, L.search, g_tm.search, ph, 1, g_tm.search_focus ? NXGLASS_FOCUSED : 0);
    }

    switch (g_tm.tab) {
        case TAB_PROC:  tm_draw_processes(c, &L); break;
        case TAB_PERF:  tm_draw_performance(c, &L); break;
        case TAB_APPS:  tm_draw_apps(c, &L); break;
        default:        tm_draw_startup(c, &L); break;
    }
    tm_draw_footer(c, &L);
    tm_draw_prio(c, &L);
    tm_draw_sheet(c, &L);
}

/* PS/2 set-1 scancodes -> ASCII for the search field (lower, upper). */
static char tm_ascii(uint8_t sc, int shift) {
    static const char lo[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./";
    static const char hi[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?";
    if (sc == 0x39) return ' ';
    if (sc >= sizeof(lo) - 1) return 0;
    return shift ? hi[sc] : lo[sc];
}

static void tm_move_selection(int delta, int visible_rows) {
    if (g_tm.tab == TAB_PROC && g_tm.nvis > 0) {
        int cur = -1;
        for (int i = 0; i < g_tm.nvis; i++)
            if (g_tm.snap.procs[g_tm.order[i]].pid == g_tm.sel_pid) { cur = i; break; }
        int nx = cur < 0 ? (delta > 0 ? 0 : g_tm.nvis - 1) : cur + delta;
        if (nx < 0) nx = 0;
        if (nx >= g_tm.nvis) nx = g_tm.nvis - 1;
        g_tm.sel_pid = g_tm.snap.procs[g_tm.order[nx]].pid;
        if (nx < g_tm.scroll) g_tm.scroll = nx;
        if (nx >= g_tm.scroll + visible_rows) g_tm.scroll = nx - visible_rows + 1;
    } else if (g_tm.tab == TAB_APPS) {
        uint32_t ids[MAX_WINDOWS];
        int n = tm_apps(ids, MAX_WINDOWS);
        if (n == 0) return;
        int cur = -1;
        for (int i = 0; i < n; i++) if (ids[i] == g_tm.sel_win) cur = i;
        int nx = cur < 0 ? 0 : cur + delta;
        if (nx < 0) nx = 0;
        if (nx >= n) nx = n - 1;
        g_tm.sel_win = ids[nx];
        if (nx < g_tm.app_scroll) g_tm.app_scroll = nx;
        if (nx >= g_tm.app_scroll + visible_rows) g_tm.app_scroll = nx - visible_rows + 1;
    }
}

void taskmgr_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx) {
    (void)ctx;
    if (!win) return;
    if (sc == 0x2A || sc == 0x36) { g_tm.shift = pressed ? 1 : 0; return; }
    if (sc == 0x1D) { g_tm.ctrl = pressed ? 1 : 0; return; }
    if (!pressed) return;

    if (g_tm.sheet_open) {
        if (sc == 0x01) g_tm.sheet_open = 0;               /* Esc */
        else if (sc == 0x1C) tm_confirm_sheet();           /* Enter */
        return;
    }
    if (g_tm.prio_open && sc == 0x01) { g_tm.prio_open = 0; return; }

    tm_layout_t L = tm_layout(win);
    tm_table_t T = (g_tm.tab == TAB_APPS) ? tm_table(&L, k_acols, AC_COUNT, L.body.y + 28.0f)
                                          : tm_table(&L, k_pcols, PC_COUNT, tm_proc_table_top(&L));
    switch (sc) {
        case 0x0F:   /* Tab: next tab (Shift: previous) */
            tm_set_tab((g_tm.tab + (g_tm.shift ? TAB_COUNT - 1 : 1)) % TAB_COUNT);
            return;
        case 0x48: tm_move_selection(-1, T.visible_rows); return;
        case 0x50: tm_move_selection(1, T.visible_rows); return;
        case 0x49: tm_move_selection(-T.visible_rows, T.visible_rows); return;
        case 0x51: tm_move_selection(T.visible_rows, T.visible_rows); return;
        case 0x47: tm_move_selection(-100000, T.visible_rows); return;
        case 0x4F: tm_move_selection(100000, T.visible_rows); return;
        case 0x53: tm_open_sheet(); return;                /* Delete: End Task */
        case 0x01:                                         /* Esc: clear search, then focus */
            if (g_tm.search_len) { g_tm.search_len = 0; g_tm.search[0] = '\0'; tm_search_changed(); }
            else g_tm.search_focus = 0;
            return;
        case 0x1C:
            if (g_tm.tab == TAB_APPS && g_tm.sel_win) desktop_activate_window(g_tm.sel_win);
            return;
        default: break;
    }
    if (!tm_has_search()) return;
    if (g_tm.ctrl && sc == 0x21) { g_tm.search_focus = 1; return; }   /* Ctrl+F */
    if (g_tm.ctrl) return;
    if (sc == 0x0E) {                                      /* Backspace */
        if (g_tm.search_len > 0) { g_tm.search[--g_tm.search_len] = '\0'; tm_search_changed(); }
        return;
    }
    char ch = tm_ascii(sc, g_tm.shift);
    if (ch && (ch != ' ' || g_tm.search_focus) && g_tm.search_len < TM_SEARCH_MAX) {
        g_tm.search[g_tm.search_len++] = ch;               /* type-to-filter */
        g_tm.search[g_tm.search_len] = '\0';
        g_tm.search_focus = 1;
        tm_search_changed();
    }
}

void taskmgr_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)ctx;
    if (!win) return;
    tm_layout_t L = tm_layout(win);
    g_tm.mx = (int32_t)L.content.x + lx;
    g_tm.my = (int32_t)L.content.y + ly;
    g_tm.pointer_in = (lx >= 0 && ly >= 0 && lx < (int32_t)win->width && ly < (int32_t)win->height);
    int click = (b & 1) && !(pb & 1);
    int release = !(b & 1) && (pb & 1);
    int32_t x = g_tm.mx, y = g_tm.my;

    /* Modal sheet */
    if (g_tm.sheet_open) {
        nxc_rect_t s = tm_sheet_rect(&L);
        nxc_rect_t cb = tm_sheet_btn(s, 1), ok = tm_sheet_btn(s, 2);
        g_tm.sheet_hover = nxglass_hit(cb, x, y) ? 1 : (nxglass_hit(ok, x, y) ? 2 : 0);
        if (click && g_tm.sheet_hover == 1) g_tm.sheet_open = 0;
        else if (click && g_tm.sheet_hover == 2) tm_confirm_sheet();
        return;
    }

    if (release) {
        int was = g_tm.pressed_btn;
        g_tm.pressed_btn = 0;
        nxc_rect_t eb = tm_footer_btn(&L, 1), sb = tm_footer_btn(&L, 2);
        if (was == 1 && nxglass_hit(eb, x, y)) tm_open_sheet();
        else if (was == 3 && nxglass_hit(sb, x, y) && g_tm.sel_win) desktop_activate_window(g_tm.sel_win);
        return;
    }
    if (!click) return;

    /* Priority popover */
    if (g_tm.prio_open) {
        nxc_rect_t r = tm_prio_rect(&L);
        if (nxglass_hit(r, x, y)) {
            int i = (int)(((float)y - r.y - 6.0f) / 28.0f);
            if (i >= 0 && i < TM_NPRIO) tm_set_prio(i);
            return;
        }
        g_tm.prio_open = 0;
        if (nxglass_hit(tm_footer_btn(&L, 2), x, y)) return;   /* toggle closed */
    }

    int seg = nxglass_segment_at(L.tabs, TAB_COUNT, x, y);
    if (seg >= 0) { tm_set_tab(seg); return; }
    if (tm_has_search() && nxglass_hit(L.search, x, y)) { g_tm.search_focus = 1; return; }
    g_tm.search_focus = 0;

    /* Footer buttons (act on release) */
    nxc_rect_t eb = tm_footer_btn(&L, 1), sb = tm_footer_btn(&L, 2);
    if (g_tm.tab == TAB_PROC || g_tm.tab == TAB_APPS) {
        if (nxglass_hit(eb, x, y)) { g_tm.pressed_btn = 1; return; }
        if (nxglass_hit(sb, x, y)) {
            if (g_tm.tab == TAB_APPS) { g_tm.pressed_btn = 3; return; }
            const nxproc_info_t *p = tm_selected();
            if (p && (g_tm.caps & NXPROC_CAP_PRIORITY) && !(p->flags & (NXPROC_F_KERNEL | NXPROC_F_SYSTEM)))
                g_tm.prio_open = 1;
            else if (p && !(g_tm.caps & NXPROC_CAP_PRIORITY))
                tm_set_status("Changing priority isn't supported by this kernel yet.", 1);
            return;
        }
    }

    if (g_tm.tab == TAB_PROC && (g_tm.snap_st == NXPROC_OK || g_tm.snap_st == NXPROC_ERR_TRUNCATED)) {
        tm_table_t T = tm_table(&L, k_pcols, PC_COUNT, tm_proc_table_top(&L));
        if (nxglass_hit(T.head, x, y)) {
            for (int i = 0; i < T.ncols; i++) {
                if ((float)x >= T.colx[i] && (float)x < T.colx[i] + T.colw[i]) {
                    if (g_tm.sort_col == i) g_tm.sort_desc = !g_tm.sort_desc;
                    else { g_tm.sort_col = i; g_tm.sort_desc = k_pcols[i].desc_first; }
                    tm_rebuild_order();
                    break;
                }
            }
            return;
        }
        if (nxglass_hit(T.rows, x, y)) {
            /* scrollbar track: page up/down */
            if ((float)x >= T.rows.x + T.rows.w - 10.0f && g_tm.nvis > T.visible_rows) {
                g_tm.scroll += ((float)y < T.rows.y + T.rows.h * 0.5f) ? -T.visible_rows : T.visible_rows;
                return;
            }
            int row = g_tm.scroll + (int)(((float)y - T.rows.y) / TM_ROW_H);
            g_tm.sel_pid = (row >= 0 && row < g_tm.nvis) ? g_tm.snap.procs[g_tm.order[row]].pid : 0;
        }
    } else if (g_tm.tab == TAB_APPS) {
        tm_table_t T = tm_table(&L, k_acols, AC_COUNT, L.body.y + 28.0f);
        if (nxglass_hit(T.rows, x, y)) {
            uint32_t ids[MAX_WINDOWS];
            int n = tm_apps(ids, MAX_WINDOWS);
            int row = g_tm.app_scroll + (int)(((float)y - T.rows.y) / TM_ROW_H);
            g_tm.sel_win = (row >= 0 && row < n) ? ids[row] : 0;
        }
    } else if (g_tm.tab == TAB_START && g_tm.startup_st == NXPROC_OK) {
        tm_table_t T = tm_table(&L, k_scols, SC_COUNT, L.body.y + 28.0f);
        if (nxglass_hit(T.rows, x, y)) {
            int row = (int)(((float)y - T.rows.y) / TM_ROW_H), k = 0;
            g_tm.sel_start = -1;
            for (uint32_t i = 0; i < g_tm.startup_n; i++) {
                if (g_tm.search_len && !tm_contains_ci(g_tm.startup[i].name, g_tm.search)) continue;
                if (k++ == row) { g_tm.sel_start = (int)i; break; }
            }
        }
    }
}
