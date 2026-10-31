/*
 * NeolyxOS - Shell (shell-hosted app view, see app_view.h)
 *
 * The NeolyxOS command shell: tabs, a prompt ("user@neolyx ~ #"), line
 * editing with history, and built-in commands that run inside the
 * desktop process on the existing userspace syscalls (files, processes,
 * network, clock). Programs on disk start with `run`; their output goes
 * to the system log because the kernel has no pipe/PTY path for apps yet.
 *
 * Commands use the same vocabulary as the kernel recovery shell
 * (kernel/src/shell/terminal.c): help, list/ls, show/cat, make dir,
 * tasks/ps, info, net status, sys <command>, ...
 *
 * Everything is static storage sized at compile time: SH_MAX_TABS tabs,
 * each with a ring of SH_LINES scrollback lines. No allocation.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "app_view.h"
#include "nxui.h"
#include "nxglass.h"
#include "nxicons.h"
#include "nxsyscall.h"
#include "nxproc.h"
#include "nxnet.h"
#include "nxconfig.h"
#include "menubar.h"

extern void serial_puts(const char *s);
extern int desktop_launch_app(const char *name);   /* desktop_shell.c: 0, -1 failed, -2 unknown */

/* ============ Limits ============ */

#define SH_MAX_TABS     6
#define SH_LINES        200         /* scrollback lines per tab (ring) */
#define SH_LINE_MAX     240         /* bytes per stored line incl. NUL */
#define SH_SPANS        16          /* color runs per stored line */
#define SH_INPUT_MAX    160         /* command line incl. NUL */
#define SH_HIST         32
#define SH_PATH_MAX     256
#define SH_ARGS_MAX     12
#define SH_NAME_MAX     32
#define SH_CAT_LIMIT    (64u * 1024u)
#define SH_DIR_MAX      128

#define SH_TABBAR_H     36.0f
#define SH_PAD_X        16.0f
#define SH_PAD_Y        10.0f
#define SH_TAB_W        150.0f
#define SH_TAB_H        24.0f

/* Colors (index into the palette of the active background) */
enum { C_TEXT = 0, C_DIM, C_CMD, C_ERR, C_OK, C_WARN, C_DIR, C_COUNT };

typedef struct { uint16_t start; uint8_t color; } sh_span_t;

typedef struct {
    char      text[SH_LINE_MAX];
    uint16_t  len;
    uint8_t   nspan;
    sh_span_t span[SH_SPANS];
} sh_line_t;

typedef struct {
    int       used;
    sh_line_t lines[SH_LINES];
    int       head, count;              /* ring: oldest index, number of lines */
    int       open;                     /* last line still receives output */
    char      input[SH_INPUT_MAX];
    int       in_len, in_cur;
    char      hist[SH_HIST][SH_INPUT_MAX];
    int       hist_count, hist_pos;     /* hist_pos == hist_count: editing a new line */
    char      draft[SH_INPUT_MAX];
    char      cwd[SH_PATH_MAX];
    char      last_cmd[SH_NAME_MAX];
    int       scroll;                   /* rows scrolled up from the bottom */
} sh_tab_t;

typedef struct {
    uint32_t win_id;
    sh_tab_t tabs[SH_MAX_TABS];
    int      cur;
    int      light;                     /* light background */
    int      shift, ctrl, alt;
    int      cols, rows;                /* text grid from the last frame */
    int32_t  mx, my;                    /* pointer, content-local */
    int      pointer_in;
    char     user[SH_NAME_MAX];
    char     host[SH_NAME_MAX];
    char     home[SH_PATH_MAX];
} sh_state_t;

static sh_state_t V;

/* Directory listing scratch (one command runs at a time). */
static nx_dirent_t g_dir[SH_DIR_MAX];

static void sh_log(const char *msg) {
    serial_puts("[SHELL] ");
    serial_puts(msg);
    serial_puts("\n");
}

static void sh_log2(const char *a, const char *b) {
    serial_puts("[SHELL] ");
    serial_puts(a);
    serial_puts(b ? b : "");
    serial_puts("\n");
}

/* ============ Small string helpers (freestanding) ============ */

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }

static int s_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int s_eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int s_starts(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

/* Bounded copy; always NUL-terminates. Returns the copied length. */
static int s_copy(char *dst, const char *src, int cap) {
    int i = 0;
    if (cap <= 0) return 0;
    while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return i;
}

/* Bounded append; returns 0, or -1 when it did not fit (dst unchanged). */
static int s_cat(char *dst, const char *src, int cap) {
    int a = s_len(dst), b = s_len(src);
    if (a + b >= cap) return -1;
    for (int i = 0; i <= b; i++) dst[a + i] = src[i];
    return 0;
}

static void s_u64(char *out, int cap, uint64_t v) {
    char tmp[24];
    int n = 0;
    do { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && n < 22);
    int i = 0;
    while (n > 0 && i < cap - 1) out[i++] = tmp[--n];
    out[i] = '\0';
}

/* "1.5 MB" style size. */
static void s_size(char *out, int cap, uint64_t bytes) {
    static const char *const unit[] = { "B", "KB", "MB", "GB", "TB" };
    int u = 0;
    uint64_t whole = bytes, frac = 0;
    while (whole >= 1024u && u < 4) { frac = (whole % 1024u) * 10u / 1024u; whole /= 1024u; u++; }
    s_u64(out, cap, whole);
    if (u > 0 && whole < 100u) {
        char f[4] = { '.', (char)('0' + (int)frac), 0, 0 };
        s_cat(out, f, cap);
    }
    s_cat(out, " ", cap);
    s_cat(out, unit[u], cap);
}

static void s_pad2(char *out, unsigned v) {
    out[0] = (char)('0' + (v / 10u) % 10u);
    out[1] = (char)('0' + v % 10u);
    out[2] = '\0';
}

/* ============ Palette ============ */

static nxc_color_t sh_color(int idx) {
    static const nxc_color_t dark[C_COUNT] = {
        NXC_RGB(0xEC, 0xEE, 0xF4),   /* text */
        NXC_RGB(0x8C, 0x93, 0xA6),   /* dim */
        NXC_RGB(0x9D, 0x86, 0xFF),   /* command word */
        NXC_RGB(0xFF, 0x5F, 0x57),   /* error / warning tag */
        NXC_RGB(0x5A, 0xE0, 0x8A),   /* success */
        NXC_RGB(0xFF, 0xB4, 0x54),   /* caution */
        NXC_RGB(0x6C, 0xB6, 0xFF),   /* directory */
    };
    static const nxc_color_t light[C_COUNT] = {
        NXC_RGB(0x1D, 0x1F, 0x26),
        NXC_RGB(0x6B, 0x70, 0x80),
        NXC_RGB(0x6A, 0x4C, 0xE8),
        NXC_RGB(0xD7, 0x2C, 0x24),
        NXC_RGB(0x1E, 0x8E, 0x4A),
        NXC_RGB(0xB0, 0x6A, 0x00),
        NXC_RGB(0x1F, 0x6F, 0xD1),
    };
    if (idx < 0 || idx >= C_COUNT) idx = C_TEXT;
    return V.light ? light[idx] : dark[idx];
}

static nxc_color_t sh_bg(void) { return V.light ? NXC_RGB(0xF7, 0xF7, 0xFA) : NXC_RGB(0x10, 0x12, 0x18); }

/* ============ Scrollback ============ */

static sh_tab_t *cur_tab(void) {
    if (V.cur < 0 || V.cur >= SH_MAX_TABS || !V.tabs[V.cur].used) return 0;
    return &V.tabs[V.cur];
}

static sh_line_t *line_at(sh_tab_t *t, int i) { return &t->lines[(t->head + i) % SH_LINES]; }

static sh_line_t *new_line(sh_tab_t *t) {
    int idx;
    if (t->count < SH_LINES) {
        idx = (t->head + t->count) % SH_LINES;
        t->count++;
    } else {
        idx = t->head;                          /* drop the oldest */
        t->head = (t->head + 1) % SH_LINES;
    }
    sh_line_t *l = &t->lines[idx];
    l->len = 0;
    l->nspan = 0;
    l->text[0] = '\0';
    t->open = 1;
    return l;
}

/* Append text in a color to the open line. '\n' ends the line. Control
 * bytes and non-ASCII are shown as '?' so the fixed grid stays aligned. */
static void out(sh_tab_t *t, int color, const char *s) {
    if (!t || !s) return;
    sh_line_t *l = t->open && t->count > 0 ? line_at(t, t->count - 1) : new_line(t);
    for (; *s; s++) {
        char ch = *s;
        if (ch == '\n') { t->open = 0; l = 0; if (s[1]) l = new_line(t); continue; }
        if (!l) l = new_line(t);
        if (ch == '\t') ch = ' ';
        else if ((uint8_t)ch < 0x20 || (uint8_t)ch >= 0x7F) ch = '?';
        if (l->len >= SH_LINE_MAX - 1) l = new_line(t);
        if (l->nspan == 0 || l->span[l->nspan - 1].color != (uint8_t)color) {
            if (l->nspan >= SH_SPANS) l = new_line(t);
            l->span[l->nspan].start = l->len;
            l->span[l->nspan].color = (uint8_t)color;
            l->nspan++;
        }
        l->text[l->len++] = ch;
        l->text[l->len] = '\0';
    }
}

static void outln(sh_tab_t *t, int color, const char *s) { out(t, color, s); out(t, color, "\n"); }

static void end_line(sh_tab_t *t) { if (t) t->open = 0; }

static void clear_tab(sh_tab_t *t) {
    t->head = 0;
    t->count = 0;
    t->open = 0;
    t->scroll = 0;
}

/* ============ Identity and paths ============ */

static int name_ok(const char *s) {
    int n = s_len(s);
    if (n < 1 || n >= SH_NAME_MAX) return 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              (c >= 'A' && c <= 'Z'))) return 0;
    }
    return 1;
}

static int dir_exists(const char *path) {
    uint32_t n = 0;
    return nx_readdir(path, g_dir, SH_DIR_MAX, &n) >= 0;
}

/* Account name from the system config (written by Setup), host name from
 * nxnet. Both fall back to fixed names when absent or malformed. */
static void load_identity(void) {
    static nxc_config_t *cfg;
    static int tried;
    if (!tried) { tried = 1; cfg = nxc_config_load(NXC_SYSTEM_CONFIG); }
    const char *u = cfg ? nxc_get_string(cfg, "account", "name", 0) : 0;
    s_copy(V.user, (u && name_ok(u)) ? u : "user", SH_NAME_MAX);

    char host[NXNET_HOSTNAME_MAX + 1];
    host[0] = '\0';
    if (nxnet_available() != NXNET_OK) (void)nxnet_init();
    if (nxnet_hostname_get(host, sizeof(host)) != NXNET_OK || !name_ok(host)) s_copy(host, "neolyx", sizeof(host));
    s_copy(V.host, host, SH_NAME_MAX);

    char home[SH_PATH_MAX];
    s_copy(home, "/Users/", SH_PATH_MAX);
    s_cat(home, V.user, SH_PATH_MAX);
    s_copy(V.home, dir_exists(home) ? home : "/", SH_PATH_MAX);
}

/* Resolve `in` against cwd into an absolute, normalized path ("." and
 * ".." removed, no repeated '/'). "~" means home. Returns 0, or -1 when
 * the input has control characters or the result does not fit. */
static int resolve_path(const char *cwd, const char *in, char *out_path, int cap) {
    char tmp[SH_PATH_MAX * 2];
    if (!in || !in[0]) in = ".";
    for (const char *p = in; *p; p++) if ((uint8_t)*p < 0x20 || (uint8_t)*p == 0x7F) return -1;
    tmp[0] = '\0';
    if (in[0] == '~' && (in[1] == '\0' || in[1] == '/')) {
        s_copy(tmp, V.home, (int)sizeof(tmp));
        if (s_cat(tmp, "/", (int)sizeof(tmp)) || s_cat(tmp, in + 1, (int)sizeof(tmp))) return -1;
    } else if (in[0] == '/') {
        s_copy(tmp, in, (int)sizeof(tmp));
    } else {
        s_copy(tmp, cwd, (int)sizeof(tmp));
        if (s_cat(tmp, "/", (int)sizeof(tmp)) || s_cat(tmp, in, (int)sizeof(tmp))) return -1;
    }
    /* Normalize component by component */
    int o = 0;
    if (cap < 2) return -1;
    out_path[o++] = '/';
    const char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/') p++;
        int n = (int)(p - s);
        if (n == 1 && s[0] == '.') continue;
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            if (o > 1) { o--; while (o > 1 && out_path[o - 1] != '/') o--; if (o > 1) o--; }
            continue;
        }
        if (o > 1) { if (o >= cap - 1) return -1; out_path[o++] = '/'; }
        if (o + n >= cap) return -1;
        for (int i = 0; i < n; i++) out_path[o++] = s[i];
    }
    out_path[o] = '\0';
    return 0;
}

/* "~" for home, "/" for root, otherwise the last component. */
static void short_dir(const char *cwd, char *buf, int cap) {
    if (s_eq(cwd, V.home) && !s_eq(V.home, "/")) { s_copy(buf, "~", cap); return; }
    if (s_eq(cwd, "/")) { s_copy(buf, "/", cap); return; }
    const char *last = cwd;
    for (const char *p = cwd; *p; p++) if (*p == '/' && p[1]) last = p + 1;
    s_copy(buf, last, cap);
}

/* Prompt pieces: "user@host dir # " */
static void prompt_text(const sh_tab_t *t, char *buf, int cap) {
    char d[SH_PATH_MAX];
    short_dir(t->cwd, d, (int)sizeof(d));
    s_copy(buf, V.user, cap);
    s_cat(buf, "@", cap);
    s_cat(buf, V.host, cap);
    s_cat(buf, " ", cap);
    s_cat(buf, d, cap);
    s_cat(buf, " # ", cap);
}

/* ============ Tabs ============ */

static void tab_title(const sh_tab_t *t, char *buf, int cap) {
    s_copy(buf, V.user, cap);
    if (t->last_cmd[0]) { s_cat(buf, " ", cap); s_cat(buf, t->last_cmd, cap); }
}

static void banner(sh_tab_t *t) {
    outln(t, C_TEXT, "Welcome to NeolyxOS Shell v1.0");
    out(t, C_DIM, "Type ");
    out(t, C_CMD, "help");
    outln(t, C_DIM, " for commands.");
}

static int tab_new(void) {
    for (int i = 0; i < SH_MAX_TABS; i++) {
        if (V.tabs[i].used) continue;
        sh_tab_t *t = &V.tabs[i];
        uint8_t *raw = (uint8_t *)t;
        for (uint32_t k = 0; k < sizeof(*t); k++) raw[k] = 0;
        t->used = 1;
        s_copy(t->cwd, V.home, SH_PATH_MAX);
        banner(t);
        V.cur = i;
        sh_log("tab opened");
        return i;
    }
    sh_log("tab limit reached");
    sh_tab_t *t = cur_tab();
    if (t) outln(t, C_WARN, "shell: tab limit reached (6 tabs)");
    return -1;
}

static int tab_count(void) {
    int n = 0;
    for (int i = 0; i < SH_MAX_TABS; i++) n += V.tabs[i].used ? 1 : 0;
    return n;
}

static void tab_step(int dir) {
    for (int k = 1; k <= SH_MAX_TABS; k++) {
        int i = ((V.cur + dir * k) % SH_MAX_TABS + SH_MAX_TABS) % SH_MAX_TABS;
        if (V.tabs[i].used) { V.cur = i; return; }
    }
}

/* Close a tab; the last tab closes the window. */
static void tab_close(int i) {
    if (i < 0 || i >= SH_MAX_TABS || !V.tabs[i].used) return;
    if (tab_count() <= 1) {
        sh_log("last tab closed, closing window");
        if (V.win_id && desktop_close_window(V.win_id) != 0) sh_log("ERROR: window close failed");
        return;
    }
    /* Wipe the tab (scrollback may hold typed secrets) */
    uint8_t *raw = (uint8_t *)&V.tabs[i];
    for (uint32_t k = 0; k < sizeof(V.tabs[i]); k++) raw[k] = 0;
    if (V.cur == i) tab_step(-1);
    if (!V.tabs[V.cur].used) tab_step(1);
    sh_log("tab closed");
}

/* ============ Commands ============ */

typedef void (*sh_cmd_fn)(sh_tab_t *t, int argc, char **argv);

typedef struct {
    const char *name;
    const char *args;
    const char *help;
    sh_cmd_fn   fn;
} sh_cmd_t;

static const sh_cmd_t *find_cmd(const char *name);
static void run_line(sh_tab_t *t, const char *line);

static void err(sh_tab_t *t, const char *cmd, const char *msg) {
    out(t, C_ERR, cmd);
    out(t, C_ERR, ": ");
    outln(t, C_TEXT, msg);
}

static void cmd_help(sh_tab_t *t, int argc, char **argv);

static void cmd_clear(sh_tab_t *t, int argc, char **argv) { (void)argc; (void)argv; clear_tab(t); }

static void cmd_echo(sh_tab_t *t, int argc, char **argv) {
    for (int i = 1; i < argc; i++) { if (i > 1) out(t, C_TEXT, " "); out(t, C_TEXT, argv[i]); }
    out(t, C_TEXT, "\n");
}

static void cmd_pwd(sh_tab_t *t, int argc, char **argv) { (void)argc; (void)argv; outln(t, C_TEXT, t->cwd); }

static void cmd_cd(sh_tab_t *t, int argc, char **argv) {
    char path[SH_PATH_MAX];
    if (argc > 2) { err(t, "cd", "too many arguments"); return; }
    if (resolve_path(t->cwd, argc > 1 ? argv[1] : "~", path, (int)sizeof(path)) != 0) {
        err(t, "cd", "invalid or too long path");
        return;
    }
    if (!dir_exists(path)) { err(t, "cd", "no such folder"); return; }
    s_copy(t->cwd, path, SH_PATH_MAX);
}

static void cmd_list(sh_tab_t *t, int argc, char **argv) {
    char path[SH_PATH_MAX];
    if (argc > 2) { err(t, argv[0], "too many arguments"); return; }
    if (resolve_path(t->cwd, argc > 1 ? argv[1] : ".", path, (int)sizeof(path)) != 0) {
        err(t, argv[0], "invalid or too long path");
        return;
    }
    uint32_t n = 0;
    int rc = nx_readdir(path, g_dir, SH_DIR_MAX, &n);
    if (rc < 0) { err(t, argv[0], "cannot read folder (it may not exist)"); return; }
    if (n > SH_DIR_MAX) n = SH_DIR_MAX;
    /* Drop "." and ".." (the kernel lists them on some filesystems) */
    uint32_t keep = 0;
    for (uint32_t i = 0; i < n; i++) {
        g_dir[i].name[NX_DIRENT_NAME_MAX] = '\0';
        if (s_eq(g_dir[i].name, ".") || s_eq(g_dir[i].name, "..")) continue;
        if (keep != i) g_dir[keep] = g_dir[i];
        keep++;
    }
    int full = n == SH_DIR_MAX;
    n = keep;
    if (n == 0) { outln(t, C_DIM, "(empty)"); return; }
    /* Columns sized to the longest name */
    int w = 1;
    for (uint32_t i = 0; i < n; i++) {
        g_dir[i].name[NX_DIRENT_NAME_MAX] = '\0';
        int l = s_len(g_dir[i].name) + (g_dir[i].type == NX_DIRENT_DIR ? 1 : 0);
        if (l > w) w = l;
    }
    int colw = w + 2;
    int per = V.cols > 0 ? V.cols / colw : 1;
    if (per < 1) per = 1;
    for (uint32_t i = 0; i < n; i++) {
        int dir = g_dir[i].type == NX_DIRENT_DIR;
        out(t, dir ? C_DIR : C_TEXT, g_dir[i].name);
        if (dir) out(t, C_DIR, "/");
        int pad = colw - s_len(g_dir[i].name) - (dir ? 1 : 0);
        if ((int)(i % (uint32_t)per) == per - 1 || i + 1 == n) { out(t, C_TEXT, "\n"); continue; }
        while (pad-- > 0) out(t, C_TEXT, " ");
    }
    if (full) outln(t, C_DIM, "(listing stopped at 128 entries)");
}

static void cmd_show(sh_tab_t *t, int argc, char **argv) {
    char path[SH_PATH_MAX];
    if (argc != 2) { err(t, argv[0], "usage: show <file>"); return; }
    if (resolve_path(t->cwd, argv[1], path, (int)sizeof(path)) != 0) {
        err(t, argv[0], "invalid or too long path");
        return;
    }
    int fd = nx_open(path, O_RDONLY);
    if (fd < 0) { err(t, argv[0], "cannot open file"); return; }
    static char buf[1025];
    uint32_t total = 0;
    int binary = 0, failed = 0;
    for (;;) {
        int64_t r = nx_read(fd, buf, 1024);
        if (r < 0) { failed = 1; break; }
        if (r == 0) break;
        if (r > 1024) r = 1024;
        for (int64_t i = 0; i < r; i++) {
            if (buf[i] == '\0') { binary = 1; break; }
            if (buf[i] == '\r') buf[i] = ' ';
        }
        if (binary) break;
        buf[r] = '\0';
        out(t, C_TEXT, buf);
        total += (uint32_t)r;
        if (total >= SH_CAT_LIMIT) break;
    }
    if (nx_close(fd) < 0) sh_log("warning: close failed after show");
    end_line(t);
    if (binary) outln(t, C_WARN, "(binary file, not shown)");
    else if (failed) err(t, argv[0], "read error");
    else if (total >= SH_CAT_LIMIT) outln(t, C_DIM, "(stopped after 64 KB)");
}

static void cmd_make(sh_tab_t *t, int argc, char **argv) {
    /* "mkdir <path>" or "make dir <path>" */
    const char *arg = 0;
    if (s_eq(argv[0], "mkdir") && argc == 2) arg = argv[1];
    else if (s_eq(argv[0], "make") && argc == 3 && s_eq(argv[1], "dir")) arg = argv[2];
    if (!arg) { err(t, argv[0], "usage: make dir <folder>"); return; }
    char path[SH_PATH_MAX];
    if (resolve_path(t->cwd, arg, path, (int)sizeof(path)) != 0 || s_eq(path, "/")) {
        err(t, argv[0], "invalid or too long path");
        return;
    }
    if (nx_mkdir(path) < 0) { err(t, argv[0], "could not create folder"); return; }
    sh_log2("folder created: ", path);
    out(t, C_OK, "created ");
    outln(t, C_TEXT, path);
}

static void cmd_date(sh_tab_t *t, int argc, char **argv) {
    (void)argc; (void)argv;
    rtc_time_t tm;
    if (nx_rtc_get(&tm) < 0 || tm.month < 1 || tm.month > 12 || tm.day < 1 || tm.day > 31) {
        err(t, "date", "clock unavailable");
        return;
    }
    char y[8], b[4];
    s_u64(y, (int)sizeof(y), tm.year);
    out(t, C_TEXT, y); out(t, C_TEXT, "-");
    s_pad2(b, tm.month);  out(t, C_TEXT, b); out(t, C_TEXT, "-");
    s_pad2(b, tm.day);    out(t, C_TEXT, b); out(t, C_TEXT, " ");
    s_pad2(b, tm.hour);   out(t, C_TEXT, b); out(t, C_TEXT, ":");
    s_pad2(b, tm.minute); out(t, C_TEXT, b); out(t, C_TEXT, ":");
    s_pad2(b, tm.second); out(t, C_TEXT, b);
    outln(t, C_DIM, " (system clock)");
}

static void put_duration(sh_tab_t *t, uint64_t ms) {
    uint64_t s = ms / 1000u;
    char n[24];
    uint64_t d = s / 86400u, h = (s / 3600u) % 24u, m = (s / 60u) % 60u, sec = s % 60u;
    if (d) { s_u64(n, (int)sizeof(n), d); out(t, C_TEXT, n); out(t, C_TEXT, "d "); }
    if (d || h) { s_u64(n, (int)sizeof(n), h); out(t, C_TEXT, n); out(t, C_TEXT, "h "); }
    s_u64(n, (int)sizeof(n), m); out(t, C_TEXT, n); out(t, C_TEXT, "m ");
    s_u64(n, (int)sizeof(n), sec); out(t, C_TEXT, n); out(t, C_TEXT, "s");
}

static void cmd_uptime(sh_tab_t *t, int argc, char **argv) {
    (void)argc; (void)argv;
    out(t, C_TEXT, "up ");
    put_duration(t, nx_gettime());
    out(t, C_TEXT, "\n");
}

static nxproc_sampler_t g_sampler;
static nxproc_snapshot_t g_snap;
static int g_sampler_ready;

static int sampler(void) {
    if (!g_sampler_ready) {
        if (nxproc_sampler_init(&g_sampler) != NXPROC_OK) return -1;
        g_sampler_ready = 1;
    }
    return 0;
}

static void pad_out(sh_tab_t *t, int color, const char *s, int width, int right) {
    int n = s_len(s);
    if (right) for (int i = n; i < width; i++) out(t, color, " ");
    out(t, color, s);
    if (!right) for (int i = n; i < width; i++) out(t, color, " ");
}

static void cmd_tasks(sh_tab_t *t, int argc, char **argv) {
    (void)argc;
    if (sampler() != 0) { err(t, argv[0], "process sampler unavailable"); return; }
    nxproc_status_t st = nxproc_snapshot(&g_sampler, &g_snap);
    if (st != NXPROC_OK && st != NXPROC_ERR_TRUNCATED) {
        err(t, argv[0], nxproc_status_str(st));
        return;
    }
    pad_out(t, C_DIM, "PID", 6, 1);
    out(t, C_DIM, "  ");
    pad_out(t, C_DIM, "NAME", 22, 0);
    pad_out(t, C_DIM, "STATE", 10, 0);
    pad_out(t, C_DIM, "MEMORY", 10, 1);
    out(t, C_DIM, "\n");
    for (uint32_t i = 0; i < g_snap.count && i < NXPROC_MAX_PROCS; i++) {
        const nxproc_info_t *p = &g_snap.procs[i];
        char n[24], name[NXPROC_NAME_MAX];
        s_u64(n, (int)sizeof(n), p->pid);
        pad_out(t, C_TEXT, n, 6, 1);
        out(t, C_TEXT, "  ");
        s_copy(name, p->name, (int)sizeof(name));
        if (s_len(name) > 21) name[21] = '\0';
        pad_out(t, (p->flags & NXPROC_F_SYSTEM) ? C_DIM : C_TEXT, name, 22, 0);
        pad_out(t, C_TEXT, nxproc_state_name(p->state), 10, 0);
        s_size(n, (int)sizeof(n), p->resident_bytes);
        pad_out(t, C_TEXT, n, 10, 1);
        out(t, C_TEXT, "\n");
    }
    if (st == NXPROC_ERR_TRUNCATED) outln(t, C_DIM, "(list truncated)");
}

static void cmd_info(sh_tab_t *t, int argc, char **argv) {
    const char *what = argc > 1 ? argv[1] : "system";
    int sys = s_eq(what, "system"), mem = s_eq(what, "memory"), cpu = s_eq(what, "cpu");
    if (!sys && !mem && !cpu) { err(t, "info", "usage: info [system|memory|cpu]"); return; }
    nxproc_sys_stats_t s;
    int have = sampler() == 0 && nxproc_sys_stats(&g_sampler, &s) == NXPROC_OK;
    char n[32];
    if (sys) {
        out(t, C_DIM, "System    "); outln(t, C_TEXT, "NeolyxOS (x86_64)");
        out(t, C_DIM, "Desktop   "); outln(t, C_TEXT, "Vajra, NXCanvas renderer");
        out(t, C_DIM, "Display   ");
        s_u64(n, (int)sizeof(n), (uint64_t)nxui_width()); out(t, C_TEXT, n); out(t, C_TEXT, " x ");
        s_u64(n, (int)sizeof(n), (uint64_t)nxui_height()); outln(t, C_TEXT, n);
        out(t, C_DIM, "Uptime    "); put_duration(t, nx_gettime()); out(t, C_TEXT, "\n");
    }
    if (sys || cpu) {
        out(t, C_DIM, "CPUs      ");
        if (have) { s_u64(n, (int)sizeof(n), s.cpu_count); outln(t, C_TEXT, n); }
        else outln(t, C_DIM, "unavailable");
        if (cpu) {
            out(t, C_DIM, "Load      ");
            if (have && (s.valid & NXPROC_SYS_CPU)) {
                s_u64(n, (int)sizeof(n), s.cpu_pct_x10 / 10u); out(t, C_TEXT, n); outln(t, C_TEXT, "%");
            } else outln(t, C_DIM, "unavailable");
        }
    }
    if (sys || mem) {
        out(t, C_DIM, "Memory    ");
        if (have && (s.valid & NXPROC_SYS_MEMORY)) {
            s_size(n, (int)sizeof(n), s.mem_used); out(t, C_TEXT, n); out(t, C_TEXT, " used of ");
            s_size(n, (int)sizeof(n), s.mem_total); out(t, C_TEXT, n);
            s_size(n, (int)sizeof(n), s.mem_free); out(t, C_DIM, " ("); out(t, C_DIM, n); outln(t, C_DIM, " free)");
        } else outln(t, C_DIM, "unavailable (kernel reports no memory figures to apps yet)");
    }
}

static void ip_out(sh_tab_t *t, int color, nxnet_ipv4_t ip) {
    char b[NXNET_IPV4_STR_MAX];
    if (nxnet_ipv4_format(ip, b, sizeof(b)) != NXNET_OK) s_copy(b, "?", (int)sizeof(b));
    out(t, color, b);
}

static void cmd_net(sh_tab_t *t, int argc, char **argv) {
    (void)argv;
    if (argc > 1 && !s_eq(argv[1], "status")) { err(t, "net", "usage: net status"); return; }
    nxnet_status_t st = nxnet_available();
    if (st != NXNET_OK) st = nxnet_init();
    if (st != NXNET_OK) {
        out(t, C_ERR, "net: ");
        out(t, C_TEXT, "networking unavailable (");
        out(t, C_TEXT, nxnet_status_str(st));
        outln(t, C_TEXT, ")");
        return;
    }
    nxnet_summary_t sum;
    if ((st = nxnet_summary(&sum)) != NXNET_OK) { err(t, "net", nxnet_status_str(st)); return; }
    static const char *const conn[] = { "offline", "link only, no address", "local network only", "connected" };
    int ci = (int)sum.connectivity;
    if (ci < 0 || ci > 3) ci = 0;
    out(t, C_DIM, "Status    ");
    outln(t, ci == 3 ? C_OK : (ci == 0 ? C_ERR : C_WARN), conn[ci]);
    static nxnet_interface_t ifs[NXNET_MAX_INTERFACES];
    uint32_t n = 0;
    st = nxnet_interfaces(ifs, NXNET_MAX_INTERFACES, &n);
    if (st != NXNET_OK && st != NXNET_ERR_BUFFER_TOO_SMALL) { err(t, "net", nxnet_status_str(st)); return; }
    if (n > NXNET_MAX_INTERFACES) n = NXNET_MAX_INTERFACES;
    static const char *const ifst[] = { "disabled", "no link", "configuring", "connected", "error" };
    for (uint32_t i = 0; i < n; i++) {
        const nxnet_interface_t *f = &ifs[i];
        char name[NXNET_IFNAME_MAX];
        s_copy(name, f->name, (int)sizeof(name));
        out(t, C_CMD, name);
        out(t, C_DIM, "  ");
        out(t, C_TEXT, nxnet_if_kind_str(f->kind));
        out(t, C_DIM, ", ");
        int si = (int)f->state;
        out(t, si == 3 ? C_OK : C_TEXT, (si >= 0 && si <= 4) ? ifst[si] : "unknown");
        if (!nxnet_ipv4_is_zero(f->ipv4.address)) { out(t, C_DIM, ", "); ip_out(t, C_TEXT, f->ipv4.address); }
        if (f->primary) out(t, C_DIM, "  (primary)");
        out(t, C_TEXT, "\n");
    }
    if (sum.wifi_present && sum.wifi_ssid[0]) {
        char ssid[NXNET_SSID_MAX + 1];
        s_copy(ssid, sum.wifi_ssid, (int)sizeof(ssid));
        out(t, C_DIM, "Wi-Fi     ");
        outln(t, C_TEXT, ssid);
    }
}

static void cmd_open(sh_tab_t *t, int argc, char **argv) {
    if (argc < 2) { err(t, "open", "usage: open <app name>"); return; }
    char name[WINDOW_TITLE_MAX];
    name[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if ((i > 1 && s_cat(name, " ", (int)sizeof(name))) || s_cat(name, argv[i], (int)sizeof(name))) {
            err(t, "open", "name too long");
            return;
        }
    }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (w && w->id && s_eq_ci(w->title, name)) {
            desktop_activate_window(w->id);
            out(t, C_OK, "focused ");
            outln(t, C_TEXT, w->title);
            return;
        }
    }
    int rc = desktop_launch_app(name);
    if (rc == 0) { sh_log2("opened app: ", name); out(t, C_OK, "opened "); outln(t, C_TEXT, name); }
    else if (rc == -2) err(t, "open", "no app with that name");
    else err(t, "open", "the app failed to start");
}

static void cmd_run(sh_tab_t *t, int argc, char **argv) {
    if (argc < 2) { err(t, argv[0], "usage: run <program> [arguments]"); return; }
    char path[SH_PATH_MAX];
    if (resolve_path(t->cwd, argv[1], path, (int)sizeof(path)) != 0) {
        err(t, argv[0], "invalid or too long path");
        return;
    }
    int fd = nx_open(path, O_RDONLY);
    if (fd < 0) { err(t, argv[0], "no such program"); return; }
    if (nx_close(fd) < 0) sh_log("warning: close failed before run");
    char *args[SH_ARGS_MAX + 1];
    int n = 0;
    args[n++] = path;
    for (int i = 2; i < argc && n < SH_ARGS_MAX; i++) args[n++] = argv[i];
    args[n] = 0;
    sh_log2("run: ", path);
    int pid = nx_spawn(path, args, 0);
    if (pid < 0) { err(t, argv[0], "the kernel refused to start the program"); return; }
    char p[16];
    s_u64(p, (int)sizeof(p), (uint64_t)pid);
    out(t, C_OK, "started ");
    out(t, C_TEXT, path);
    out(t, C_DIM, " (pid ");
    out(t, C_DIM, p);
    outln(t, C_DIM, "; its output goes to the system log)");
}

static void cmd_history(sh_tab_t *t, int argc, char **argv) {
    (void)argc; (void)argv;
    for (int i = 0; i < t->hist_count; i++) {
        char n[8];
        s_u64(n, (int)sizeof(n), (uint64_t)(i + 1));
        pad_out(t, C_DIM, n, 4, 1);
        out(t, C_DIM, "  ");
        outln(t, C_TEXT, t->hist[i]);
    }
}

static void cmd_whoami(sh_tab_t *t, int argc, char **argv) { (void)argc; (void)argv; outln(t, C_TEXT, V.user); }
static void cmd_hostname(sh_tab_t *t, int argc, char **argv) { (void)argc; (void)argv; outln(t, C_TEXT, V.host); }

static void cmd_version(sh_tab_t *t, int argc, char **argv) {
    (void)argc; (void)argv;
    outln(t, C_TEXT, "NeolyxOS Shell 1.0 (Vajra desktop)");
}

static void cmd_exit(sh_tab_t *t, int argc, char **argv) {
    (void)t; (void)argc; (void)argv;
    tab_close(V.cur);
}

/* `sys <command>`: system-level commands. The desktop has no privilege
 * service to elevate through yet (there is no accounts or privilege
 * syscall for apps), so nothing here claims elevated rights. */
static void cmd_sys(sh_tab_t *t, int argc, char **argv) {
    if (argc < 2) {
        err(t, "sys", "usage: sys <command>   (sys status, sys root, sys info)");
        return;
    }
    if (s_eq(argv[1], "root")) {
        sh_log("sys root requested: refused, no privilege service");
        out(t, C_ERR, "[Warning!]");
        outln(t, C_TEXT, " sys root needs the NeolyxOS accounts service, which is not available yet.");
        outln(t, C_DIM, "You are still signed in as a normal user. Nothing was changed.");
        return;
    }
    if (s_eq(argv[1], "status")) {
        out(t, C_DIM, "User        "); outln(t, C_TEXT, V.user);
        out(t, C_DIM, "Privilege   "); outln(t, C_TEXT, "user");
        out(t, C_DIM, "Elevation   "); outln(t, C_WARN, "unavailable (no accounts service)");
        return;
    }
    if (s_eq(argv[1], "info")) { cmd_info(t, argc - 1, argv + 1); return; }
    if (find_cmd(argv[1])) {
        err(t, "sys", "this command does not need sys; run it without the prefix");
        return;
    }
    err(t, "sys", "unknown system command (try sys status)");
}

static const sh_cmd_t g_cmds[] = {
    { "help",     "[command]",       "Show commands",                     cmd_help },
    { "clear",    "",                "Clear the screen (Ctrl L)",         cmd_clear },
    { "echo",     "<text>",          "Print text",                        cmd_echo },
    { "pwd",      "",                "Show the current folder",           cmd_pwd },
    { "cd",       "[folder]",        "Change folder (~ is home)",         cmd_cd },
    { "list",     "[folder]",        "List a folder",                     cmd_list },
    { "ls",       "[folder]",        "List a folder",                     cmd_list },
    { "show",     "<file>",          "Print a text file",                 cmd_show },
    { "cat",      "<file>",          "Print a text file",                 cmd_show },
    { "make",     "dir <folder>",    "Create a folder",                   cmd_make },
    { "mkdir",    "<folder>",        "Create a folder",                   cmd_make },
    { "tasks",    "",                "List running processes",            cmd_tasks },
    { "ps",       "",                "List running processes",            cmd_tasks },
    { "info",     "[system|memory|cpu]", "System information",            cmd_info },
    { "net",      "status",          "Network status",                    cmd_net },
    { "date",     "",                "Show date and time",                cmd_date },
    { "uptime",   "",                "Time since boot",                   cmd_uptime },
    { "open",     "<app>",           "Open or focus an app",              cmd_open },
    { "run",      "<program> [args]","Start a program from disk",         cmd_run },
    { "exec",     "<program> [args]","Start a program from disk",         cmd_run },
    { "history",  "",                "Commands typed in this tab",        cmd_history },
    { "whoami",   "",                "Current user",                      cmd_whoami },
    { "hostname", "",                "This computer's name",              cmd_hostname },
    { "version",  "",                "Shell version",                     cmd_version },
    { "sys",      "<command>",       "System commands (sys status)",      cmd_sys },
    { "exit",     "",                "Close this tab",                    cmd_exit },
};
#define CMD_COUNT ((int)(sizeof(g_cmds) / sizeof(g_cmds[0])))

static const sh_cmd_t *find_cmd(const char *name) {
    for (int i = 0; i < CMD_COUNT; i++) if (s_eq(g_cmds[i].name, name)) return &g_cmds[i];
    return 0;
}

static void cmd_help(sh_tab_t *t, int argc, char **argv) {
    if (argc > 1) {
        const sh_cmd_t *c = find_cmd(argv[1]);
        if (!c) { err(t, "help", "no such command"); return; }
        out(t, C_CMD, c->name); out(t, C_TEXT, " "); outln(t, C_TEXT, c->args);
        outln(t, C_DIM, c->help);
        return;
    }
    for (int i = 0; i < CMD_COUNT; i++) {
        pad_out(t, C_CMD, g_cmds[i].name, 10, 0);
        pad_out(t, C_TEXT, g_cmds[i].args, 20, 0);
        outln(t, C_DIM, g_cmds[i].help);
    }
    outln(t, C_DIM, "Keys: Up/Down history, Tab completes, Ctrl C cancels, Ctrl T new tab, Ctrl W close tab");
}

/* Split a line into words; double quotes group words. Returns argc, or
 * -1 on an unterminated quote. Writes into buf. */
static int split(const char *line, char *buf, int cap, char **argv, int max) {
    int argc = 0, o = 0;
    const char *p = line;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (argc >= max || o >= cap - 1) break;
        argv[argc++] = &buf[o];
        int quoted = 0;
        while (*p && (quoted || *p != ' ')) {
            if (*p == '"') { quoted = !quoted; p++; continue; }
            if (o >= cap - 1) break;
            buf[o++] = *p++;
        }
        if (quoted) return -1;
        buf[o++] = '\0';
    }
    if (o < cap) buf[o] = '\0';
    return argc;
}

/* Echo "prompt + command" into the scrollback, the first word in the
 * command color, then run it. */
static void run_line(sh_tab_t *t, const char *line) {
    char prompt[SH_PATH_MAX + 2 * SH_NAME_MAX];
    prompt_text(t, prompt, (int)sizeof(prompt));
    end_line(t);
    out(t, C_TEXT, prompt);
    const char *p = line;
    while (*p == ' ') p++;
    const char *w = p;
    while (*p && *p != ' ') p++;
    char word[SH_INPUT_MAX];
    int wl = (int)(p - w);
    if (wl >= SH_INPUT_MAX) wl = SH_INPUT_MAX - 1;
    for (int i = 0; i < wl; i++) word[i] = w[i];
    word[wl] = '\0';
    char lead[SH_INPUT_MAX];
    int ll = (int)(w - line);
    for (int i = 0; i < ll && i < SH_INPUT_MAX - 1; i++) lead[i] = ' ';
    lead[ll < SH_INPUT_MAX ? ll : SH_INPUT_MAX - 1] = '\0';
    out(t, C_TEXT, lead);
    out(t, C_CMD, word);
    out(t, C_TEXT, p);
    out(t, C_TEXT, "\n");
    t->scroll = 0;
    if (!word[0]) return;

    static char buf[SH_INPUT_MAX];
    char *argv[SH_ARGS_MAX];
    int argc = split(line, buf, (int)sizeof(buf), argv, SH_ARGS_MAX);
    if (argc < 0) { err(t, "shell", "missing closing quote"); return; }
    if (argc == 0) return;
    const sh_cmd_t *c = find_cmd(argv[0]);
    if (!c) {
        out(t, C_ERR, "shell: ");
        out(t, C_TEXT, "command not found: ");
        out(t, C_CMD, argv[0]);
        outln(t, C_DIM, "  (type help)");
        return;
    }
    s_copy(t->last_cmd, argv[0], SH_NAME_MAX);
    if (argc > 1 && s_len(t->last_cmd) + s_len(argv[1]) + 1 < SH_NAME_MAX && !s_eq(argv[0], "echo")) {
        s_cat(t->last_cmd, " ", SH_NAME_MAX);
        s_cat(t->last_cmd, argv[1], SH_NAME_MAX);
    }
    c->fn(t, argc, argv);
    end_line(t);
}

/* ============ Line editing ============ */

static void set_input(sh_tab_t *t, const char *s) {
    t->in_len = s_copy(t->input, s, SH_INPUT_MAX);
    t->in_cur = t->in_len;
}

static void history_add(sh_tab_t *t, const char *line) {
    if (!line[0]) return;
    if (t->hist_count > 0 && s_eq(t->hist[t->hist_count - 1], line)) return;
    if (t->hist_count == SH_HIST) {
        for (int i = 1; i < SH_HIST; i++) s_copy(t->hist[i - 1], t->hist[i], SH_INPUT_MAX);
        t->hist_count--;
    }
    s_copy(t->hist[t->hist_count++], line, SH_INPUT_MAX);
}

static void submit(sh_tab_t *t) {
    char line[SH_INPUT_MAX];
    s_copy(line, t->input, SH_INPUT_MAX);
    set_input(t, "");
    history_add(t, line);
    t->hist_pos = t->hist_count;
    run_line(t, line);
}

static void insert_char(sh_tab_t *t, char ch) {
    if (t->in_len >= SH_INPUT_MAX - 1) return;
    for (int i = t->in_len; i > t->in_cur; i--) t->input[i] = t->input[i - 1];
    t->input[t->in_cur++] = ch;
    t->input[++t->in_len] = '\0';
}

static void delete_at(sh_tab_t *t, int pos) {
    if (pos < 0 || pos >= t->in_len) return;
    for (int i = pos; i < t->in_len; i++) t->input[i] = t->input[i + 1];
    t->in_len--;
    if (t->in_cur > pos) t->in_cur--;
}

/* Tab: complete the command word. One match fills it in; several are listed. */
static void complete(sh_tab_t *t) {
    for (int i = 0; i < t->in_cur; i++) if (t->input[i] == ' ') return;   /* only the first word */
    char pre[SH_INPUT_MAX];
    s_copy(pre, t->input, t->in_cur + 1);
    int matches = 0, last = -1;
    for (int i = 0; i < CMD_COUNT; i++) if (s_starts(g_cmds[i].name, pre)) { matches++; last = i; }
    if (matches == 1) {
        char rest[SH_INPUT_MAX];
        s_copy(rest, t->input + t->in_cur, SH_INPUT_MAX);
        char line[SH_INPUT_MAX];
        s_copy(line, g_cmds[last].name, SH_INPUT_MAX);
        if (!rest[0]) s_cat(line, " ", SH_INPUT_MAX);
        int cur = s_len(line);
        if (s_cat(line, rest, SH_INPUT_MAX) == 0) { set_input(t, line); t->in_cur = cur; }
    } else if (matches > 1) {
        char prompt[SH_PATH_MAX + 2 * SH_NAME_MAX];
        prompt_text(t, prompt, (int)sizeof(prompt));
        end_line(t);
        out(t, C_TEXT, prompt);
        outln(t, C_TEXT, t->input);
        for (int i = 0; i < CMD_COUNT; i++)
            if (s_starts(g_cmds[i].name, pre)) { out(t, C_CMD, g_cmds[i].name); out(t, C_TEXT, "  "); }
        out(t, C_TEXT, "\n");
    }
}

static char sc_ascii(uint8_t sc, int shift) {
    static const char lo[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./";
    static const char hi[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?";
    if (sc == 0x39) return ' ';
    if (sc >= sizeof(lo) - 1) return 0;
    return shift ? hi[sc] : lo[sc];
}

/* ============ Menu bar (Shell File View Window Tab Setting Help) ============ */

enum {
    SA_NEW_TAB = MENUBAR_APP_ACTION_BASE, SA_CLOSE_TAB, SA_NEXT_TAB, SA_PREV_TAB,
    SA_CLEAR, SA_TOP, SA_BOTTOM, SA_DARK, SA_LIGHT, SA_HELP,
};

/* Standard actions of menubar.c handled by the shell (desktop_menu_action) */
#define STD_CLOSE_WINDOW 4
#define STD_SHORTCUTS    22
#define STD_ABOUT        23

static const menubar_item_t g_m_file[] = {
    { "New Tab",      "Ctrl T", 1, SA_NEW_TAB },
    { "Close Tab",    "Ctrl W", 1, SA_CLOSE_TAB },
    { NULL, NULL, 0, 0 },
    { "Close Window", "NX W",   1, STD_CLOSE_WINDOW },
};
static const menubar_item_t g_m_view[] = {
    { "Clear Screen",     "Ctrl L",  1, SA_CLEAR },
    { NULL, NULL, 0, 0 },
    { "Scroll to Top",    "Shift PgUp", 1, SA_TOP },
    { "Scroll to Bottom", "Shift PgDn", 1, SA_BOTTOM },
};
static const menubar_item_t g_m_tab[] = {
    { "New Tab",          "Ctrl T",         1, SA_NEW_TAB },
    { "Next Tab",         "Ctrl Tab",       1, SA_NEXT_TAB },
    { "Previous Tab",     "Shift Ctrl Tab", 1, SA_PREV_TAB },
    { NULL, NULL, 0, 0 },
    { "Close Tab",        "Ctrl W",         1, SA_CLOSE_TAB },
};
static const menubar_item_t g_m_setting[] = {
    { "Dark Background",  NULL, 1, SA_DARK },
    { "Light Background", NULL, 1, SA_LIGHT },
};
static const menubar_item_t g_m_help[] = {
    { "Shell Commands",     NULL,      1, SA_HELP },
    { NULL, NULL, 0, 0 },
    { "Keyboard Shortcuts", "Hold NX", 1, STD_SHORTCUTS },
    { "About NeolyxOS",     NULL,      1, STD_ABOUT },
};

#define NITEMS(a) ((int)(sizeof(a) / sizeof(a[0])))
static menubar_menu_t g_shell_menus[6];

static void scroll_by(sh_tab_t *t, int rows);

static void shell_menu_action(int menu, int action) {
    (void)menu;
    sh_tab_t *t = cur_tab();
    switch (action) {
        case SA_NEW_TAB:   tab_new(); break;
        case SA_CLOSE_TAB: tab_close(V.cur); break;
        case SA_NEXT_TAB:  tab_step(1); break;
        case SA_PREV_TAB:  tab_step(-1); break;
        case SA_CLEAR:     if (t) clear_tab(t); break;
        case SA_TOP:       if (t) scroll_by(t, 1 << 20); break;
        case SA_BOTTOM:    if (t) t->scroll = 0; break;
        case SA_DARK:      V.light = 0; sh_log("background: dark"); break;
        case SA_LIGHT:     V.light = 1; sh_log("background: light"); break;
        case SA_HELP:      if (t) run_line(t, "help"); break;
        default:           sh_log("menu action not handled"); break;
    }
}

static void register_menus(void) {
    static int done;
    if (done) return;
    const menubar_menu_t *win = menubar_standard_menu("Window");
    if (!win) { sh_log("ERROR: standard Window menu missing; keeping standard menus"); return; }
    g_shell_menus[0] = (menubar_menu_t){ "File",    g_m_file,    NITEMS(g_m_file) };
    g_shell_menus[1] = (menubar_menu_t){ "View",    g_m_view,    NITEMS(g_m_view) };
    g_shell_menus[2] = *win;
    g_shell_menus[3] = (menubar_menu_t){ "Tab",     g_m_tab,     NITEMS(g_m_tab) };
    g_shell_menus[4] = (menubar_menu_t){ "Setting", g_m_setting, NITEMS(g_m_setting) };
    g_shell_menus[5] = (menubar_menu_t){ "Help",    g_m_help,    NITEMS(g_m_help) };
    if (menubar_register_app_menus("Shell", g_shell_menus, 6, shell_menu_action) == 0) done = 1;
    else sh_log("ERROR: could not register Shell menus");
}

/* ============ Layout ============ */

static float g_cw = 7.5f, g_lh = 17.0f;   /* cell width, line height */

static void measure(void) {
    float w = nxui_text_width(NXUI_TEXT_MONO, "MMMMMMMMMM") / 10.0f;
    if (w > 3.0f && w < 40.0f) g_cw = w;
    float lh = nxui_text_line_height(NXUI_TEXT_MONO) + 2.0f;
    if (lh > 8.0f && lh < 60.0f) g_lh = lh;
}

static nxc_rect_t tab_rect(nxc_rect_t content, int slot) {
    return nxc_rect(content.x + 12.0f + (float)slot * (SH_TAB_W + 6.0f), content.y + 6.0f, SH_TAB_W, SH_TAB_H);
}

static nxc_rect_t tab_close_rect(nxc_rect_t tr) {
    return nxc_rect(tr.x + tr.w - 22.0f, tr.y + 4.0f, 16.0f, 16.0f);
}

static nxc_rect_t plus_rect(nxc_rect_t content, int n) {
    nxc_rect_t r = tab_rect(content, n);
    return nxc_rect(r.x, r.y, 24.0f, SH_TAB_H);
}

static nxc_rect_t text_rect(nxc_rect_t content) {
    return nxc_rect(content.x + SH_PAD_X, content.y + SH_TABBAR_H + SH_PAD_Y,
                    content.w - 2.0f * SH_PAD_X, content.h - SH_TABBAR_H - 2.0f * SH_PAD_Y);
}

static int line_rows(int len, int cols) { return len <= 0 ? 1 : (len + cols - 1) / cols; }

static int total_rows(const sh_tab_t *t, int cols, int live_len) {
    int rows = 0;
    for (int i = 0; i < t->count; i++) rows += line_rows(t->lines[(t->head + i) % SH_LINES].len, cols);
    return rows + line_rows(live_len + 1, cols);
}

static void scroll_by(sh_tab_t *t, int rows) {
    char prompt[SH_PATH_MAX + 2 * SH_NAME_MAX];
    prompt_text(t, prompt, (int)sizeof(prompt));
    int cols = V.cols > 0 ? V.cols : 80;
    int max = total_rows(t, cols, s_len(prompt) + t->in_len) - (V.rows > 0 ? V.rows : 1);
    if (max < 0) max = 0;
    long s = (long)t->scroll + rows;
    if (s < 0) s = 0;
    if (s > max) s = max;
    t->scroll = (int)s;
}

/* ============ Rendering ============ */

/* Draw text[a, b) of a styled line at grid row y, starting at column 0. */
static void draw_segment(nxc_canvas_t *c, float x0, float base, const char *text, const sh_span_t *spans,
                         int nspan, int a, int b) {
    char buf[SH_LINE_MAX];
    for (int s = 0; s < nspan; s++) {
        int s0 = spans[s].start, s1 = (s + 1 < nspan) ? spans[s + 1].start : b;
        if (s1 > b) s1 = b;
        if (s0 < a) s0 = a;
        if (s0 >= s1) continue;
        int n = s1 - s0;
        if (n >= SH_LINE_MAX) n = SH_LINE_MAX - 1;
        for (int i = 0; i < n; i++) buf[i] = text[s0 + i];
        buf[n] = '\0';
        nxui_text(c, NXUI_TEXT_MONO, x0 + (float)(s0 - a) * g_cw, base, buf, sh_color(spans[s].color), NULL);
    }
}

static void draw_tabs(nxc_canvas_t *c, nxc_rect_t content) {
    const nxui_theme_t *th = nxui_theme();
    nxc_color_t chip = V.light ? NXC_RGB(0xE6, 0xE7, 0xEE) : NXC_RGB(0x22, 0x25, 0x2F);
    nxc_color_t chip_on = V.light ? NXC_RGB(0xFF, 0xFF, 0xFF) : NXC_RGB(0x2E, 0x32, 0x3F);
    nxc_color_t edge = V.light ? NXC_ARGB(40, 0, 0, 0) : NXC_ARGB(50, 0xFF, 0xFF, 0xFF);
    int slot = 0;
    for (int i = 0; i < SH_MAX_TABS; i++) {
        if (!V.tabs[i].used) continue;
        nxc_rect_t r = tab_rect(content, slot++);
        if (r.x + r.w > content.x + content.w - 40.0f) break;
        int on = i == V.cur;
        int hover = V.pointer_in && nxglass_hit(nxc_rect(r.x - content.x, r.y - content.y, r.w, r.h), V.mx, V.my);
        nxc_fill_rrect(c, r, 7.0f, on ? chip_on : (hover ? nxc_color_lerp(chip, chip_on, 0.5f) : chip));
        nxc_stroke_rrect(c, r, 7.0f, 1.0f, on ? NXC_WITH_ALPHA(th->accent, 160) : edge);
        char title[SH_NAME_MAX * 2];
        tab_title(&V.tabs[i], title, (int)sizeof(title));
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(r.x + 10.0f, r.y, r.w - 38.0f, r.h), title,
                          on ? sh_color(C_TEXT) : sh_color(C_DIM), NXC_ALIGN_LEFT);
        nxc_rect_t x = tab_close_rect(r);
        nxc_fill_rrect(c, x, 4.0f, NXC_RGB(0xE5, 0x3E, 0x3E));
        nxsym_draw(c, NXSYM_CLOSE, x.x + x.w * 0.5f, x.y + x.h * 0.5f, 7.0f, NXC_RGB(0xFF, 0xFF, 0xFF), 0.0f);
    }
    nxc_rect_t p = plus_rect(content, slot);
    if (p.x + p.w <= content.x + content.w - 8.0f && tab_count() < SH_MAX_TABS) {
        nxc_fill_rrect(c, p, 7.0f, chip);
        nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, p, "+", sh_color(C_DIM), NXC_ALIGN_CENTER);
    }
    nxc_fill_rect(c, nxc_rect(content.x, content.y + SH_TABBAR_H - 1.0f, content.w, 1.0f),
                  V.light ? NXC_ARGB(24, 0, 0, 0) : NXC_ARGB(28, 0xFF, 0xFF, 0xFF));
}

void shell_view_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    if (!win) return;
    nxc_canvas_t *c = nxui_canvas();
    if (!c) return;
    const nxui_theme_t *th = nxui_theme();
    measure();
    nxc_rect_t content = app_view_content_rect(win);

    /* Background, rounded at the bottom with the window */
    nxc_save(c);
    nxc_clip_rrect(c, nxc_rect((float)win->x, (float)win->y, (float)win->width,
                               (float)win->height + APP_VIEW_TITLE_H), th->radius_window);
    nxc_fill_rect(c, content, sh_bg());
    nxc_restore(c);

    draw_tabs(c, content);

    sh_tab_t *t = cur_tab();
    if (!t) return;
    nxc_rect_t tr = text_rect(content);
    int cols = (int)(tr.w / g_cw), rows = (int)(tr.h / g_lh);
    if (cols < 8) cols = 8;
    if (rows < 1) rows = 1;
    V.cols = cols;
    V.rows = rows;

    char prompt[SH_PATH_MAX + 2 * SH_NAME_MAX];
    prompt_text(t, prompt, (int)sizeof(prompt));
    int plen = s_len(prompt);
    int total = total_rows(t, cols, plen + t->in_len);
    int max_scroll = total - rows > 0 ? total - rows : 0;
    if (t->scroll > max_scroll) t->scroll = max_scroll;
    int first = total - rows - t->scroll;     /* first visible grid row */
    if (first < 0) first = 0;

    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)tr.x, (int32_t)tr.y, (int32_t)tr.w + 2, (int32_t)tr.h + 2));
    float asc = nxui_text_ascent(NXUI_TEXT_MONO);
    int row = 0;
    for (int i = 0; i < t->count && row < first + rows; i++) {
        const sh_line_t *l = line_at(t, i);
        int nr = line_rows(l->len, cols);
        for (int k = 0; k < nr; k++, row++) {
            if (row < first || row >= first + rows) continue;
            int a = k * cols, b = a + cols;
            if (b > l->len) b = l->len;
            float base = tr.y + (float)(row - first) * g_lh + asc + 1.0f;
            draw_segment(c, tr.x, base, l->text, l->span, l->nspan, a, b);
        }
    }

    /* Live prompt line: prompt, then the command word in color */
    {
        char live[SH_LINE_MAX];
        sh_span_t sp[4];
        int n = s_copy(live, prompt, SH_LINE_MAX);
        int ns = 0;
        sp[ns++] = (sh_span_t){ 0, C_TEXT };
        int w0 = 0;
        while (w0 < t->in_len && t->input[w0] == ' ') w0++;
        int w1 = w0;
        while (w1 < t->in_len && t->input[w1] != ' ') w1++;
        if (w1 > w0) {
            sp[ns++] = (sh_span_t){ (uint16_t)(plen + w0), C_CMD };
            sp[ns++] = (sh_span_t){ (uint16_t)(plen + w1), C_TEXT };
        }
        for (int i = 0; i < t->in_len && n < SH_LINE_MAX - 1; i++) live[n++] = t->input[i];
        live[n] = '\0';
        int nr = line_rows(n + 1, cols);
        for (int k = 0; k < nr; k++, row++) {
            float y = tr.y + (float)(row - first) * g_lh;
            if (row < first || row >= first + rows) continue;
            int a = k * cols, b = a + cols;
            if (b > n) b = n;
            if (a < b) draw_segment(c, tr.x, y + asc + 1.0f, live, sp, ns, a, b);
        }
        /* Cursor (blinks while the window has focus; hollow when not) */
        int pos = plen + t->in_cur;
        int crow = total - nr + pos / cols - first;
        if (t->scroll == 0 && crow >= 0 && crow < rows) {
            float cx = tr.x + (float)(pos % cols) * g_cw;
            float cy = tr.y + (float)crow * g_lh;
            nxc_rect_t cr = nxc_rect(cx, cy + 1.0f, 2.0f, g_lh - 2.0f);
            if (!win->focused) nxc_stroke_rrect(c, nxc_rect(cx, cy + 1.0f, g_cw, g_lh - 2.0f), 1.0f, 1.0f, sh_color(C_DIM));
            else if ((int)(nxui_time() * 1.8f) % 2 == 0) nxc_fill_rect(c, cr, th->accent);
        }
    }
    nxc_restore(c);

    /* Scrolled up: a hint at the bottom edge */
    if (t->scroll > 0) {
        nxc_rect_t hint = nxc_rect(content.x + content.w - 150.0f, content.y + content.h - 30.0f, 136.0f, 22.0f);
        nxc_fill_rrect(c, hint, 11.0f, NXC_WITH_ALPHA(th->accent, 220));
        nxui_text_in_rect(c, NXUI_TEXT_CAPTION, hint, "Shift PgDn: latest", th->text_on_accent, NXC_ALIGN_CENTER);
    }
}

/* ============ Input ============ */

void shell_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx) {
    (void)win; (void)ctx;
    if (sc == 0x2A || sc == 0x36) { V.shift = pressed ? 1 : 0; return; }
    if (sc == 0x1D) { V.ctrl = pressed ? 1 : 0; return; }
    if (sc == 0x38) { V.alt = pressed ? 1 : 0; return; }
    if (!pressed) return;
    sh_tab_t *t = cur_tab();
    if (!t) return;

    if (V.ctrl) {
        switch (sc) {
            case 0x14: tab_new(); return;                                   /* T */
            case 0x11: tab_close(V.cur); return;                            /* W */
            case 0x0F: tab_step(V.shift ? -1 : 1); return;                  /* Tab */
            case 0x26: clear_tab(t); return;                                /* L */
            case 0x2E:                                                      /* C */
                end_line(t);
                { char p[SH_PATH_MAX + 2 * SH_NAME_MAX]; prompt_text(t, p, (int)sizeof(p)); out(t, C_TEXT, p); }
                out(t, C_TEXT, t->input);
                outln(t, C_DIM, "^C");
                set_input(t, "");
                t->hist_pos = t->hist_count;
                t->scroll = 0;
                return;
            case 0x16: set_input(t, ""); return;                            /* U */
            case 0x1E: t->in_cur = 0; return;                               /* A */
            case 0x12: t->in_cur = t->in_len; return;                       /* E */
            default: return;
        }
    }
    if (V.alt) return;

    switch (sc) {
        case 0x1C: submit(t); return;                                       /* Enter */
        case 0x0E: if (t->in_cur > 0) delete_at(t, t->in_cur - 1); return;   /* Backspace */
        case 0x53: delete_at(t, t->in_cur); return;                         /* Delete */
        case 0x4B: if (t->in_cur > 0) t->in_cur--; return;                  /* Left */
        case 0x4D: if (t->in_cur < t->in_len) t->in_cur++; return;          /* Right */
        case 0x47: t->in_cur = 0; return;                                   /* Home */
        case 0x4F: t->in_cur = t->in_len; return;                           /* End */
        case 0x0F: complete(t); return;                                     /* Tab */
        case 0x01: set_input(t, ""); t->hist_pos = t->hist_count; return;   /* Esc */
        case 0x49: scroll_by(t, V.shift ? (1 << 20) : (V.rows > 2 ? V.rows - 2 : 1)); return;   /* PgUp */
        case 0x51: if (V.shift) t->scroll = 0; else scroll_by(t, -(V.rows > 2 ? V.rows - 2 : 1)); return;
        case 0x48:                                                          /* Up: older */
            if (t->hist_pos > 0) {
                if (t->hist_pos == t->hist_count) s_copy(t->draft, t->input, SH_INPUT_MAX);
                t->hist_pos--;
                set_input(t, t->hist[t->hist_pos]);
            }
            return;
        case 0x50:                                                          /* Down: newer */
            if (t->hist_pos < t->hist_count) {
                t->hist_pos++;
                set_input(t, t->hist_pos == t->hist_count ? t->draft : t->hist[t->hist_pos]);
            }
            return;
        default: break;
    }
    char ch = sc_ascii(sc, V.shift);
    if (ch) { insert_char(t, ch); t->scroll = 0; }
}

void shell_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)ctx;
    if (!win) return;
    V.mx = lx;
    V.my = ly;
    V.pointer_in = 1;
    int press = (b & 1) && !(pb & 1);
    if (!press) return;
    /* Hit test in local coordinates (content top-left = 0,0) */
    nxc_rect_t local = nxc_rect(0.0f, 0.0f, (float)win->width, (float)win->height);
    int slot = 0;
    for (int i = 0; i < SH_MAX_TABS; i++) {
        if (!V.tabs[i].used) continue;
        nxc_rect_t r = tab_rect(local, slot++);
        if (r.x + r.w > local.w - 40.0f) break;
        if (nxglass_hit(tab_close_rect(r), lx, ly)) { tab_close(i); return; }
        if (nxglass_hit(r, lx, ly)) { V.cur = i; return; }
    }
    if (tab_count() < SH_MAX_TABS && nxglass_hit(plus_rect(local, slot), lx, ly)) tab_new();
}

/* ============ Lifecycle ============ */

int shell_view_open(uint32_t win_id) {
    if (win_id == 0) return -1;
    uint8_t *raw = (uint8_t *)&V;
    for (uint32_t i = 0; i < sizeof(V); i++) raw[i] = 0;
    V.win_id = win_id;
    V.cur = -1;
    V.cols = 80;
    V.rows = 24;
    load_identity();
    register_menus();
    if (tab_new() < 0) return -1;
    sh_log2("opened for ", V.user);
    return 0;
}
