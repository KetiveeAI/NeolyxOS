/*
 * NeolyxOS - Notes: the native plain-text note editor
 *
 * A sidebar lists the notes in ~/Documents/Notes and the editor shows the
 * selected note. The first line of a note
 * is its title: it is drawn larger, shown in the sidebar, and becomes the
 * file name when the note is saved ("Shopping list.txt").
 *
 *   typing, Return, Tab        edit (Tab inserts four spaces)
 *   arrows, Home/End, PgUp/Dn  move; with Shift they select
 *   Ctrl Left/Right            move by word; Ctrl Home/End: start/end
 *   Ctrl Up/Down               previous / next note
 *   Ctrl N / S                 new note / save now
 *   Ctrl Z / Shift Ctrl Z / Y  undo / redo
 *   Ctrl X / C / V / A         cut / copy / paste / select all
 *   click, drag, double-click  place the caret, select, select a word
 *
 * Storage and safety:
 *   - Notes are plain UTF-8 text files with '\n' line ends, one per note.
 *   - Saving never overwrites in place: the text is written to a hidden
 *     ".<name>.saving" file next to the note, its size is verified, and
 *     only then is the old file replaced (remove + rename). If the system
 *     stops between those two steps, the next listing finds the
 *     ".saving" file and puts it back under the note's name.
 *   - Changes are saved 1.5 s after the last edit, when the window loses
 *     focus, before switching notes and on Ctrl S. A failed save keeps the
 *     text in memory, says why in the toolbar and is not retried until the
 *     next edit. Switching notes is refused while the current note cannot
 *     be saved, so text is never dropped silently.
 *   - "Delete" moves the note into Notes/Recently Deleted (a rename, which
 *     can be undone in Path); nothing is erased.
 *   - A note larger than NV_TEXT_MAX opens read-only instead of being
 *     truncated on the next save.
 *
 * Folder and file names go through the Path relay (nxpath_relay.h) for
 * validation and listing; file contents use the kernel file syscalls.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "app_view.h"
#include "nxpath_relay.h"
#include "nxui.h"
#include "nxicons.h"
#include "nxsyscall.h"
#include "menubar.h"

extern int desktop_launch_app(const char *name);

/* ============ Limits and layout ============ */

#define NV_TEXT_MAX      32768u      /* bytes per note, including the NUL */
#define NV_UNDO_MAX      8
#define NV_REDO_MAX      4
#define NV_MAX_NOTES     128
#define NV_VL_MAX        4096        /* wrapped lines kept in the layout */
#define NV_TITLE_MAX     48          /* bytes of the first line used as a file name */
#define NV_PREVIEW_MAX   72
#define NV_DRAW_MAX      1024        /* bytes of one wrapped line passed to the renderer */
#define NV_AUTOSAVE_MS   1500u
#define NV_DBL_MS        450u
#define NV_STATUS_MS     4000u
#define NV_MAGIC         0x4E4F544553564945ULL   /* "NOTESVIE" */

#define NV_SIDEBAR_W     232.0f
#define NV_TOOLBAR_H      44.0f
#define NV_STATUS_H       26.0f
#define NV_ROW_H          54.0f
#define NV_PAD_X          28.0f
#define NV_PAD_Y          18.0f

#define NV_STYLE_TITLE   NXUI_TEXT_TITLE2
#define NV_STYLE_BODY    NXUI_TEXT_CALLOUT

#define NV_EXT_TXT       ".txt"
#define NV_TRASH_NAME    "Recently Deleted"

/* Edit kinds for undo grouping: consecutive edits of one kind form one step. */
enum { NV_EDIT_NONE = 0, NV_EDIT_TYPE, NV_EDIT_DELETE, NV_EDIT_OTHER };

typedef struct {
    char     text[NV_TEXT_MAX];
    uint32_t len, cur;
} nv_snap_t;

typedef struct {
    nxc_rect_t sidebar, side_head, plus, list, side_foot;
    nxc_rect_t toolbar, b_path, b_trash, editor, status;
    int rows_visible;
} nv_layout_t;

static struct {
    uint64_t magic;              /* NV_MAGIC once initialized (BSS may not be zeroed) */
    uint32_t win_id;
    char     folder[NXRELAY_PATH_MAX];       /* ~/Documents/Notes */

    /* The open document */
    char     doc_dir[NXRELAY_PATH_MAX];
    char     doc_name[NXRELAY_NAME_MAX + 1]; /* "" = new note, not on disk yet */
    int      auto_name;          /* file name follows the first line (notes folder) */
    int      readonly;
    char     text[NV_TEXT_MAX];
    uint32_t len;
    uint32_t cur, anchor;        /* caret and selection anchor (byte offsets) */
    float    goal_x;             /* column kept while moving up/down */
    int      goal_valid;
    int      dirty, save_failed, last_kind;
    uint64_t edit_ms;

    /* Message in the status bar */
    char     status[128];
    int      status_err;
    uint64_t status_ms;

    /* Wrapped layout of text (rebuilt after edits and resizes) */
    uint32_t vl_start[NV_VL_MAX];
    uint32_t vl_len[NV_VL_MAX];  /* excludes the '\n' */
    float    vl_y[NV_VL_MAX];
    uint8_t  vl_title[NV_VL_MAX];
    int      vl_n;
    float    content_h, layout_w, view_h;
    int      layout_dirty, vl_overflow_logged;
    uint32_t words, chars;
    float    scroll;

    /* Sidebar */
    char     names[NV_MAX_NOTES][NXRELAY_NAME_MAX + 1];
    char     previews[NV_MAX_NOTES][NV_PREVIEW_MAX];
    int      n_notes, list_truncated, list_scroll, hover_row, hover_btn;

    /* Input */
    int      shift, ctrl, alt, dragging;
    uint64_t last_click_ms;
    uint32_t last_click_pos;
    char     clip[NV_TEXT_MAX];
    uint32_t clip_len;
} N;

/* A file to open once the window exists (outside N: the first open clears N). */
static char g_pending_open[NXRELAY_PATH_MAX];

static nv_snap_t g_undo[NV_UNDO_MAX], g_redo[NV_REDO_MAX];
static int g_undo_base, g_undo_n, g_redo_base, g_redo_n;

static float g_cw[2][128];       /* cached advance widths: [0] title, [1] body */
static int   g_cw_ready;

/* ============ Small helpers ============ */

static void nv_log(const char *msg, const char *arg) {
    char line[NXRELAY_PATH_MAX + 96];
    int n = 0;
    for (const char *s = "[NOTES] "; *s; s++) line[n++] = *s;
    for (const char *s = msg; s && *s && n < (int)sizeof(line) - 2; s++) line[n++] = *s;
    if (arg) {
        if (n < (int)sizeof(line) - 2) line[n++] = ' ';
        for (const char *s = arg; *s && n < (int)sizeof(line) - 2; s++) line[n++] = *s;
    }
    line[n++] = '\n';
    line[n] = '\0';
    nx_debug_print(line);
}

static void nv_strcpy(char *d, const char *s, int cap) {
    int i = 0;
    if (cap <= 0) return;
    for (; s && i < cap - 1 && s[i]; i++) d[i] = s[i];
    d[i] = '\0';
}

/* Append s to d (cap bytes). Returns 0, or -1 when it did not fit (d is left terminated). */
static int nv_strcat(char *d, const char *s, int cap) {
    int n = 0;
    while (n < cap && d[n]) n++;
    if (n >= cap) return -1;
    for (; *s; s++) {
        if (n >= cap - 1) { d[n] = '\0'; return -1; }
        d[n++] = *s;
    }
    d[n] = '\0';
    return 0;
}

static int nv_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int nv_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char nv_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int nv_streq_ci(const char *a, const char *b) {
    while (*a && nv_lower(*a) == nv_lower(*b)) { a++; b++; }
    return nv_lower(*a) == nv_lower(*b);
}

static int nv_ends_ci(const char *s, const char *suffix) {
    int a = nv_strlen(s), b = nv_strlen(suffix);
    return a >= b && nv_streq_ci(s + a - b, suffix);
}

static void nv_move_bytes(char *dst, const char *src, uint32_t n) {
    if (dst == src || n == 0) return;
    if (dst < src) for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
    else for (uint32_t i = n; i > 0; i--) dst[i - 1] = src[i - 1];
}

static void nv_utoa(uint32_t v, char *out, int cap) {
    char tmp[12];
    int n = 0, o = 0;
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11);
    while (n && o < cap - 1) out[o++] = tmp[--n];
    out[o] = '\0';
}

static int nv_is_cont(char c) { return ((uint8_t)c & 0xC0u) == 0x80u; }

static uint32_t nv_next(uint32_t p) {
    if (p >= N.len) return N.len;
    p++;
    while (p < N.len && nv_is_cont(N.text[p])) p++;
    return p;
}

static uint32_t nv_prev(uint32_t p) {
    if (p == 0) return 0;
    p--;
    while (p > 0 && nv_is_cont(N.text[p])) p--;
    return p;
}

static int nv_in(nxc_rect_t r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void nv_set_status(const char *msg, int is_err) {
    nv_strcpy(N.status, msg, (int)sizeof(N.status));
    N.status_err = is_err;
    N.status_ms = nx_gettime();
    if (is_err) nv_log("error:", msg);
}

static const char *nv_fs_err(int rc) {
    switch (rc) {
        case NX_FS_NOT_FOUND: return "the file or folder is missing";
        case NX_FS_PROTECTED: return "this location is protected by the system";
        case NX_FS_EXISTS:    return "a file with that name already exists";
        case NX_FS_NOT_DIR:   return "part of the path is not a folder";
        case NX_FS_BUSY:      return "the file is in use";
        case NX_FS_INVALID:   return "the name is not allowed here";
        case NX_FS_READ_ONLY: return "the file or disk is read-only";
        case NX_FS_IO:        return "the disk reported an error";
        default:              return "the system refused the change";
    }
}

/* ============ Titles and names ============ */

/* Title of a text: its first line, trimmed and made safe as a file name
 * (no \ / : * ? " < > |, no control characters, no leading dots), at most
 * NV_TITLE_MAX bytes without splitting a UTF-8 sequence. Empty -> "". */
static void nv_title_of(const char *text, uint32_t len, char *out, int cap) {
    int o = 0, space = 0;
    int lim = cap - 1 < NV_TITLE_MAX ? cap - 1 : NV_TITLE_MAX;
    for (uint32_t i = 0; i < len && text[i] != '\n'; i++) {
        char c = text[i];
        if ((uint8_t)c < 0x20 || c == 0x7F || c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = ' ';
        if (c == ' ') { space = 1; continue; }
        if (o == 0 && c == '.') continue;
        int need = space && o > 0 ? 2 : 1;
        if (o + need > lim) break;
        if (space && o > 0) out[o++] = ' ';
        space = 0;
        out[o++] = c;
    }
    while (o > 0 && nv_is_cont(out[o - 1])) o--;              /* never end mid-sequence */
    if (o > 0 && ((uint8_t)out[o - 1] & 0xC0u) == 0xC0u) o--;  /* dangling lead byte */
    while (o > 0 && (out[o - 1] == '.' || out[o - 1] == ' ')) o--;
    out[o] = '\0';
}

/* Display title of a file name: the name without its extension. */
static void nv_stem(const char *name, char *out, int cap) {
    nv_strcpy(out, name, cap);
    int n = nv_strlen(out);
    for (int i = n - 1; i > 0; i--) if (out[i] == '.') { out[i] = '\0'; break; }
}

/* The second non-empty line of a text, for the sidebar preview. */
static void nv_preview_of(const char *text, uint32_t len, char *out, int cap) {
    uint32_t i = 0;
    while (i < len && text[i] != '\n') i++;          /* skip the title line */
    while (i < len && (text[i] == '\n' || text[i] == ' ')) i++;
    int o = 0;
    for (; i < len && text[i] != '\n' && o < cap - 1; i++)
        out[o++] = (uint8_t)text[i] < 0x20 ? ' ' : text[i];
    while (o > 0 && nv_is_cont(out[o - 1])) o--;
    if (o > 0 && ((uint8_t)out[o - 1] & 0xC0u) == 0xC0u) o--;
    out[o] = '\0';
}

static int nv_is_note_name(const char *name) {
    return name[0] != '.' && (nv_ends_ci(name, NV_EXT_TXT) || nv_ends_ci(name, ".md"));
}

/* A free "<stem>[ k]<ext>" in dir. `self` (may be "") counts as free: it is
 * the document being saved. Returns NXRELAY_OK or an error. */
static int nv_unique_name(const char *dir, const char *stem, const char *ext, const char *self,
                          char *out, int cap) {
    for (uint32_t k = 1; k < 100; k++) {
        char name[NXRELAY_NAME_MAX + 1], num[12], path[NXRELAY_PATH_MAX];
        nv_strcpy(name, stem, (int)sizeof(name));
        if (k > 1) {
            nv_utoa(k, num, (int)sizeof(num));
            if (nv_strcat(name, " ", (int)sizeof(name)) || nv_strcat(name, num, (int)sizeof(name)))
                return NXRELAY_ERR_INVALID;
        }
        if (nv_strcat(name, ext, (int)sizeof(name))) return NXRELAY_ERR_INVALID;
        if (self && self[0] && nv_streq_ci(name, self)) { nv_strcpy(out, name, cap); return NXRELAY_OK; }
        if (nxrelay_join(dir, name, path, (int)sizeof(path)) != NXRELAY_OK) return NXRELAY_ERR_INVALID;
        int r = nxrelay_stat(path, NULL, NULL);
        if (r == NXRELAY_ERR_NOT_FOUND) { nv_strcpy(out, name, cap); return NXRELAY_OK; }
        if (r != NXRELAY_OK) return r;
    }
    return NXRELAY_ERR_INVALID;
}

/* ============ Files ============ */

/* Create `path` as a folder (and nothing else) if missing. */
static int nv_mkdir_if_missing(const char *path) {
    nxrelay_kind_t kind;
    int r = nxrelay_stat(path, &kind, NULL);
    if (r == NXRELAY_OK) return kind == NXRELAY_KIND_FOLDER ? NXRELAY_OK : NXRELAY_ERR_NOT_DIR;
    if (r != NXRELAY_ERR_NOT_FOUND) return r;
    int m = nx_mkdir(path);
    if (m != 0 && !(nxrelay_stat(path, &kind, NULL) == NXRELAY_OK && kind == NXRELAY_KIND_FOLDER)) {
        nv_log("could not create folder", path);
        return NXRELAY_ERR_IO;
    }
    nv_log("created folder", path);
    return NXRELAY_OK;
}

/* Make sure ~/Documents/Notes exists (creating /Users, home and Documents as needed). */
static int nv_ensure_folder(void) {
    char home[NXRELAY_PATH_MAX], docs[NXRELAY_PATH_MAX];
    if (nxrelay_location(NXRELAY_LOC_HOME, home, (int)sizeof(home)) != NXRELAY_OK ||
        nxrelay_location(NXRELAY_LOC_DOCUMENTS, docs, (int)sizeof(docs)) != NXRELAY_OK)
        return NXRELAY_ERR_INVALID;
    int r = nv_mkdir_if_missing("/Users");
    if (r == NXRELAY_OK && !nv_streq(home, "/Users")) r = nv_mkdir_if_missing(home);
    if (r == NXRELAY_OK) r = nv_mkdir_if_missing(docs);
    if (r == NXRELAY_OK) r = nv_mkdir_if_missing(N.folder);
    return r;
}

/* Write data to path (created or truncated) and verify the size on disk. */
static int nv_write_file(const char *path, const char *data, uint32_t len) {
    int fd = nx_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { nv_log("open for writing failed:", path); return NXRELAY_ERR_DENIED; }
    uint32_t done = 0;
    int rc = NXRELAY_OK;
    while (done < len) {
        int64_t w = nx_write(fd, data + done, (uint64_t)(len - done));
        if (w <= 0) { rc = NXRELAY_ERR_IO; break; }
        done += (uint32_t)w;
    }
    if (nx_close(fd) < 0 && rc == NXRELAY_OK) rc = NXRELAY_ERR_IO;
    if (rc != NXRELAY_OK) { nv_log("write failed:", path); return rc; }
    uint64_t size = 0;
    nxrelay_kind_t kind;
    if (nxrelay_stat(path, &kind, &size) != NXRELAY_OK || kind != NXRELAY_KIND_FILE || size != len) {
        nv_log("size check after write failed:", path);
        return NXRELAY_ERR_IO;
    }
    return NXRELAY_OK;
}

/* Read up to cap-1 bytes of path into buf (NUL-terminated). *total gets
 * the file size. Returns bytes read or a negative relay error. */
static int64_t nv_read_file(const char *path, char *buf, uint32_t cap, uint64_t *total) {
    nxrelay_kind_t kind;
    uint64_t size = 0;
    int r = nxrelay_stat(path, &kind, &size);
    if (r != NXRELAY_OK) return r;
    if (kind != NXRELAY_KIND_FILE) return NXRELAY_ERR_INVALID;
    if (total) *total = size;
    int fd = nx_open(path, O_RDONLY);
    if (fd < 0) return NXRELAY_ERR_DENIED;
    uint32_t got = 0;
    while (got < cap - 1) {
        int64_t n = nx_read(fd, buf + got, (uint64_t)(cap - 1 - got));
        if (n < 0) { nx_close(fd); return NXRELAY_ERR_IO; }
        if (n == 0) break;
        got += (uint32_t)n;
    }
    nx_close(fd);
    buf[got] = '\0';
    return (int64_t)got;
}

/* Clean text read from disk in place: CRLF/CR become LF, tabs become four
 * spaces (while room remains), NUL and other control bytes are dropped. */
static uint32_t nv_clean_text(char *t, uint32_t len, uint32_t cap) {
    uint32_t o = 0;
    for (uint32_t i = 0; i < len; i++) {
        char c = t[i];
        if (c == '\r') { if (i + 1 < len && t[i + 1] == '\n') continue; c = '\n'; }
        if (c == '\t') {
            uint32_t rest = len - i - 1;
            if (o + 4 + rest < cap) {
                nv_move_bytes(t + o + 4, t + i + 1, rest);
                for (int k = 0; k < 4; k++) t[o++] = ' ';
                len = o + rest;
                i = o - 1;
                continue;
            }
            c = ' ';
        }
        if (((uint8_t)c < 0x20 && c != '\n') || c == 0x7F) continue;
        t[o++] = c;
    }
    t[o] = '\0';
    return o;
}

/* ============ Undo ============ */

static void nv_snap_take(nv_snap_t *s) {
    nv_move_bytes(s->text, N.text, N.len);
    s->len = N.len;
    s->cur = N.cur;
}

static void nv_snap_restore(const nv_snap_t *s) {
    uint32_t n = s->len < NV_TEXT_MAX - 1 ? s->len : NV_TEXT_MAX - 1;
    nv_move_bytes(N.text, s->text, n);
    N.len = n;
    N.text[n] = '\0';
    N.cur = N.anchor = s->cur <= n ? s->cur : n;
}

static void nv_history_clear(void) { g_undo_base = g_undo_n = g_redo_base = g_redo_n = 0; N.last_kind = NV_EDIT_NONE; }

static void nv_undo_push(void) {
    int idx;
    if (g_undo_n == NV_UNDO_MAX) { idx = g_undo_base; g_undo_base = (g_undo_base + 1) % NV_UNDO_MAX; }
    else idx = (g_undo_base + g_undo_n++) % NV_UNDO_MAX;
    nv_snap_take(&g_undo[idx]);
}

static void nv_redo_push(void) {
    int idx;
    if (g_redo_n == NV_REDO_MAX) { idx = g_redo_base; g_redo_base = (g_redo_base + 1) % NV_REDO_MAX; }
    else idx = (g_redo_base + g_redo_n++) % NV_REDO_MAX;
    nv_snap_take(&g_redo[idx]);
}

static void nv_changed(void) {
    N.dirty = 1;
    N.save_failed = 0;
    N.edit_ms = nx_gettime();
    N.layout_dirty = 1;
    N.goal_valid = 0;
}

/* Start an edit of `kind`: a new kind (or any OTHER edit) begins an undo step. */
static void nv_begin_edit(int kind) {
    if (kind != N.last_kind || kind == NV_EDIT_OTHER) {
        nv_undo_push();
        g_redo_n = 0;
    }
    N.last_kind = kind;
}

static void nv_undo(void) {
    if (N.readonly) return;
    if (g_undo_n == 0) { nv_set_status("Nothing to undo", 0); return; }
    nv_redo_push();
    g_undo_n--;
    nv_snap_restore(&g_undo[(g_undo_base + g_undo_n) % NV_UNDO_MAX]);
    N.last_kind = NV_EDIT_NONE;
    nv_changed();
}

static void nv_redo(void) {
    if (N.readonly) return;
    if (g_redo_n == 0) { nv_set_status("Nothing to redo", 0); return; }
    nv_undo_push();
    g_redo_n--;
    nv_snap_restore(&g_redo[(g_redo_base + g_redo_n) % NV_REDO_MAX]);
    N.last_kind = NV_EDIT_NONE;
    nv_changed();
}

/* ============ Editing ============ */

static int nv_selection(uint32_t *a, uint32_t *b) {
    uint32_t x = N.cur < N.anchor ? N.cur : N.anchor;
    uint32_t y = N.cur < N.anchor ? N.anchor : N.cur;
    if (a) *a = x;
    if (b) *b = y;
    return x != y;
}

static int nv_refuse_readonly(void) {
    if (!N.readonly) return 0;
    nv_set_status("This note is read-only", 1);
    return 1;
}

static void nv_delete_range(uint32_t a, uint32_t b) {
    if (b > N.len) b = N.len;
    if (a >= b) return;
    nv_move_bytes(N.text + a, N.text + b, N.len - b);
    N.len -= b - a;
    N.text[N.len] = '\0';
    N.cur = N.anchor = a;
    nv_changed();
}

/* Insert n bytes (selection replaced). Bytes other than '\n', printable
 * ASCII and UTF-8 are dropped. Stops at NV_TEXT_MAX with a message. */
static void nv_insert(const char *s, uint32_t n, int kind) {
    if (nv_refuse_readonly()) return;
    uint32_t a, b;
    nv_begin_edit(kind);
    if (nv_selection(&a, &b)) nv_delete_range(a, b);
    uint32_t room = NV_TEXT_MAX - 1 - N.len;
    uint32_t clean = 0;
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (((uint8_t)c >= 0x20 && c != 0x7F) || c == '\n') clean++;
    }
    if (clean > room) {
        nv_set_status("The note is full (32 KB). Start a new note to keep writing.", 1);
        clean = room;
    }
    if (clean == 0) return;
    nv_move_bytes(N.text + N.cur + clean, N.text + N.cur, N.len - N.cur);
    uint32_t o = N.cur;
    for (uint32_t i = 0; i < n && o < N.cur + clean; i++) {
        char c = s[i];
        if (((uint8_t)c >= 0x20 && c != 0x7F) || c == '\n') N.text[o++] = c;
    }
    N.len += clean;
    N.text[N.len] = '\0';
    N.cur = N.anchor = o;
    nv_changed();
}

static void nv_backspace(int word) {
    if (nv_refuse_readonly()) return;
    uint32_t a, b;
    if (nv_selection(&a, &b)) { nv_begin_edit(NV_EDIT_OTHER); nv_delete_range(a, b); return; }
    if (N.cur == 0) return;
    uint32_t p = nv_prev(N.cur);
    if (word) {
        while (p > 0 && N.text[p] == ' ') p = nv_prev(p);
        while (p > 0 && N.text[nv_prev(p)] != ' ' && N.text[nv_prev(p)] != '\n') p = nv_prev(p);
    }
    nv_begin_edit(NV_EDIT_DELETE);
    nv_delete_range(p, N.cur);
}

static void nv_delete_fwd(void) {
    if (nv_refuse_readonly()) return;
    uint32_t a, b;
    if (nv_selection(&a, &b)) { nv_begin_edit(NV_EDIT_OTHER); nv_delete_range(a, b); return; }
    if (N.cur >= N.len) return;
    nv_begin_edit(NV_EDIT_DELETE);
    nv_delete_range(N.cur, nv_next(N.cur));
}

static void nv_copy(int cut) {
    uint32_t a, b;
    if (!nv_selection(&a, &b)) { nv_set_status("Select some text first", 0); return; }
    nv_move_bytes(N.clip, N.text + a, b - a);
    N.clip_len = b - a;
    if (cut) {
        if (nv_refuse_readonly()) return;
        nv_begin_edit(NV_EDIT_OTHER);
        nv_delete_range(a, b);
    }
}

static void nv_paste(void) {
    if (N.clip_len == 0) { nv_set_status("Nothing to paste. Copy text in Notes first.", 0); return; }
    nv_insert(N.clip, N.clip_len, NV_EDIT_OTHER);
}

/* ============ Layout ============ */

static float nv_lh(int title) {
    return nxui_text_line_height(title ? NV_STYLE_TITLE : NV_STYLE_BODY) + (title ? 8.0f : 6.0f);
}

static void nv_measure(void) {
    if (g_cw_ready) return;
    nxui_text_style_t st[2] = { NV_STYLE_TITLE, NV_STYLE_BODY };
    for (int s = 0; s < 2; s++) {
        float nn = nxui_text_width(st[s], "nn");
        for (int c = 0; c < 128; c++) {
            if (c < 0x20 || c == 0x7F) { g_cw[s][c] = 0.0f; continue; }
            char b[4] = { 'n', (char)c, 'n', 0 };   /* between letters: spaces keep their width */
            float w = nxui_text_width(st[s], b) - nn;
            g_cw[s][c] = w > 0.0f ? w : 1.0f;
        }
    }
    g_cw_ready = 1;
}

static float nv_char_w(int title, char c) {
    uint8_t u = (uint8_t)c;
    if (u < 0x80) return g_cw[title ? 0 : 1][u];
    if ((u & 0xC0u) == 0x80u) return 0.0f;
    return g_cw[title ? 0 : 1]['n'];
}

static void nv_emit(uint32_t start, uint32_t len, int title, float y) {
    N.vl_start[N.vl_n] = start;
    N.vl_len[N.vl_n] = len;
    N.vl_title[N.vl_n] = (uint8_t)title;
    N.vl_y[N.vl_n] = y;
    N.vl_n++;
}

/* Wrap the text into lines no wider than `width` (breaking after spaces,
 * or inside a word longer than the line). The first logical line is the
 * title. Also counts words and characters for the status bar. */
static void nv_layout(float width) {
    nv_measure();
    if (width < 40.0f) width = 40.0f;
    N.vl_n = 0;
    float y = 0.0f;
    uint32_t pos = 0;
    int logical = 0;
    for (;;) {
        uint32_t start = pos, end = pos;
        while (end < N.len && N.text[end] != '\n') end++;
        int title = logical == 0;
        float lh = nv_lh(title);
        if (start == end) {
            if (N.vl_n < NV_VL_MAX) { nv_emit(start, 0, title, y); y += lh; }
        }
        uint32_t s = start;
        while (s < end) {
            if (N.vl_n >= NV_VL_MAX - 1) {
                /* Out of line slots: the rest of the text stays on one line (clipped). */
                if (!N.vl_overflow_logged) { nv_log("layout line limit reached; long note is clipped", 0); N.vl_overflow_logged = 1; }
                nv_emit(s, N.len - s, title, y);
                y += lh;
                N.content_h = y;
                goto counted;
            }
            float w = 0.0f;
            uint32_t i = s, brk;
            int32_t last_space = -1;
            while (i < end) {
                float cw = nv_char_w(title, N.text[i]);
                if (w + cw > width && i > s && !nv_is_cont(N.text[i])) break;
                if (N.text[i] == ' ') last_space = (int32_t)i;
                w += cw;
                i++;
            }
            if (i < end) {
                brk = last_space >= (int32_t)s ? (uint32_t)last_space + 1 : i;
                while (brk > s + 1 && nv_is_cont(N.text[brk])) brk--;
            } else {
                brk = end;
            }
            nv_emit(s, brk - s, title, y);
            y += lh;
            s = brk;
        }
        if (title) y += 6.0f;      /* a little air under the title */
        if (end >= N.len) break;
        pos = end + 1;
        logical++;
    }
    N.content_h = y;
counted:
    N.words = N.chars = 0;
    int in_word = 0;
    for (uint32_t i = 0; i < N.len; i++) {
        char c = N.text[i];
        if (!nv_is_cont(c) && c != '\n') N.chars++;
        int sp = c == ' ' || c == '\n';
        if (!sp && !in_word) N.words++;
        in_word = !sp;
    }
    N.layout_w = width;
    N.layout_dirty = 0;
}

static void nv_relayout(float width) {
    if (N.layout_dirty || width != N.layout_w || N.vl_n == 0) nv_layout(width);
}

/* Wrapped line holding byte offset pos. */
static int nv_vline_of(uint32_t pos) {
    int lo = 0, hi = N.vl_n - 1;
    if (hi < 0) return 0;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (N.vl_start[mid] <= pos) lo = mid; else hi = mid - 1;
    }
    return lo;
}

static nxui_text_style_t nv_style(int vi) { return N.vl_title[vi] ? NV_STYLE_TITLE : NV_STYLE_BODY; }

/* Pixel offset of pos inside wrapped line vi. */
static float nv_x_of(int vi, uint32_t pos) {
    if (vi < 0 || vi >= N.vl_n) return 0.0f;
    uint32_t s = N.vl_start[vi];
    if (pos <= s) return 0.0f;
    uint32_t n = pos - s;
    if (n >= NV_DRAW_MAX) n = NV_DRAW_MAX - 1;
    char buf[NV_DRAW_MAX];
    nv_move_bytes(buf, N.text + s, n);
    buf[n] = '\0';
    /* Measure with a trailing letter so a final space keeps its width. */
    if (buf[n - 1] == ' ' && n + 1 < NV_DRAW_MAX) {
        buf[n] = 'n';
        buf[n + 1] = '\0';
        return nxui_text_width(nv_style(vi), buf) - g_cw[N.vl_title[vi] ? 0 : 1]['n'];
    }
    return nxui_text_width(nv_style(vi), buf);
}

/* Last caret position inside wrapped line vi (a wrapped line ends before
 * its trailing space, so the caret does not jump to the next line). */
static uint32_t nv_line_end(int vi) {
    uint32_t end = N.vl_start[vi] + N.vl_len[vi];
    int wrapped = vi + 1 < N.vl_n && N.vl_start[vi + 1] == end && end < N.len && N.text[end] != '\n';
    if (wrapped && end > N.vl_start[vi]) end = nv_prev(end);
    return end;
}

static uint32_t nv_pos_at_x(int vi, float x) {
    if (vi < 0) vi = 0;
    if (vi >= N.vl_n) vi = N.vl_n - 1;
    uint32_t p = N.vl_start[vi], lim = nv_line_end(vi);
    float w = 0.0f;
    while (p < lim) {
        uint32_t q = nv_next(p);
        float cw = 0.0f;
        for (uint32_t k = p; k < q; k++) cw += nv_char_w(N.vl_title[vi], N.text[k]);
        if (x < w + cw * 0.5f) return p;
        w += cw;
        p = q;
    }
    return lim;
}

static void nv_ensure_visible(void) {
    if (N.vl_n == 0 || N.view_h <= 0.0f) return;
    int vi = nv_vline_of(N.cur);
    float top = N.vl_y[vi], bot = top + nv_lh(N.vl_title[vi]);
    if (top < N.scroll) N.scroll = top;
    if (bot > N.scroll + N.view_h) N.scroll = bot - N.view_h;
    if (N.scroll < 0.0f) N.scroll = 0.0f;
}

/* ============ Caret movement ============ */

static void nv_move(uint32_t pos, int select) {
    if (pos > N.len) pos = N.len;
    N.cur = pos;
    if (!select) N.anchor = pos;
    N.last_kind = NV_EDIT_NONE;
}

static void nv_move_vert(int lines, int select) {
    nv_relayout(N.layout_w);
    int vi = nv_vline_of(N.cur);
    if (!N.goal_valid) { N.goal_x = nv_x_of(vi, N.cur); N.goal_valid = 1; }
    int t = vi + lines;
    if (t < 0) { nv_move(0, select); return; }
    if (t >= N.vl_n) { nv_move(N.len, select); return; }
    float gx = N.goal_x;
    nv_move(nv_pos_at_x(t, gx), select);
    N.goal_valid = 1;
    N.goal_x = gx;
}

static uint32_t nv_word_left(uint32_t p) {
    while (p > 0 && (N.text[p - 1] == ' ' || N.text[p - 1] == '\n')) p--;
    while (p > 0 && N.text[p - 1] != ' ' && N.text[p - 1] != '\n') p--;
    return p;
}

static uint32_t nv_word_right(uint32_t p) {
    while (p < N.len && (N.text[p] == ' ' || N.text[p] == '\n')) p++;
    while (p < N.len && N.text[p] != ' ' && N.text[p] != '\n') p++;
    return p;
}

/* ============ Sidebar list ============ */

static void nv_read_preview(const char *path, char *out, int cap) {
    char head[512];
    out[0] = '\0';
    int64_t n = nv_read_file(path, head, (uint32_t)sizeof(head), NULL);
    if (n <= 0) return;
    uint32_t len = nv_clean_text(head, (uint32_t)n, (uint32_t)sizeof(head));
    nv_preview_of(head, len, out, cap);
}

/* Put back a note whose save stopped between "remove old" and "rename new". */
static int nv_recover(const nxrelay_listing_t *l) {
    int fixed = 0;
    for (uint32_t i = 0; i < l->count; i++) {
        const char *nm = l->entries[i].name;
        int n = nv_strlen(nm);
        if (nm[0] != '.' || n <= 8 || !nv_ends_ci(nm, ".saving") || l->entries[i].kind != NXRELAY_KIND_FILE) continue;
        char real[NXRELAY_NAME_MAX + 1];
        int rl = n - 1 - 7;
        if (rl <= 0 || rl > NXRELAY_NAME_MAX) continue;
        for (int k = 0; k < rl; k++) real[k] = nm[1 + k];
        real[rl] = '\0';
        int present = 0;
        for (uint32_t j = 0; j < l->count; j++) if (nv_streq(l->entries[j].name, real)) present = 1;
        char tmp[NXRELAY_PATH_MAX], dst[NXRELAY_PATH_MAX];
        if (nxrelay_join(N.folder, nm, tmp, (int)sizeof(tmp)) != NXRELAY_OK ||
            nxrelay_join(N.folder, real, dst, (int)sizeof(dst)) != NXRELAY_OK) continue;
        if (present) { nv_log("left an unfinished save in place (the note itself is intact):", tmp); continue; }
        int rc = nx_rename(tmp, dst);
        if (rc == NX_FS_OK) { nv_log("recovered an interrupted save as", dst); fixed++; }
        else nv_log("could not recover interrupted save:", tmp);
    }
    return fixed;
}

static void nv_refresh_list(void) {
    nxrelay_invalidate(N.folder);
    const nxrelay_listing_t *l = nxrelay_list(N.folder, NXRELAY_REFRESH | NXRELAY_SHOW_HIDDEN);
    if (l->status == NXRELAY_OK && nv_recover(l) > 0) {
        nxrelay_invalidate(N.folder);
        l = nxrelay_list(N.folder, NXRELAY_REFRESH | NXRELAY_SHOW_HIDDEN);
    }
    N.n_notes = 0;
    N.list_truncated = 0;
    if (l->status != NXRELAY_OK) {
        if (l->status != NXRELAY_ERR_NOT_FOUND) nv_log("cannot list notes folder:", N.folder);
        return;   /* missing folder: created on the first save */
    }
    /* Copy names first: reading previews must not touch the relay listing. */
    for (uint32_t i = 0; i < l->count; i++) {
        if (l->entries[i].kind != NXRELAY_KIND_FILE || !nv_is_note_name(l->entries[i].name)) continue;
        if (N.n_notes >= NV_MAX_NOTES) { N.list_truncated = 1; break; }
        nv_strcpy(N.names[N.n_notes], l->entries[i].name, NXRELAY_NAME_MAX + 1);
        N.n_notes++;
    }
    for (int i = 0; i < N.n_notes; i++) {
        char p[NXRELAY_PATH_MAX];
        N.previews[i][0] = '\0';
        if (nxrelay_join(N.folder, N.names[i], p, (int)sizeof(p)) == NXRELAY_OK)
            nv_read_preview(p, N.previews[i], NV_PREVIEW_MAX);
    }
}

/* Index of the open document in the list, or -1. */
static int nv_current_row(void) {
    if (!N.doc_name[0] || !nv_streq(N.doc_dir, N.folder)) return -1;
    for (int i = 0; i < N.n_notes; i++) if (nv_streq(N.names[i], N.doc_name)) return i;
    return -1;
}

/* Rows shown: an unsaved new note sits on top. */
static int nv_has_draft_row(void) { return N.auto_name && !N.doc_name[0]; }
static int nv_row_count(void) { return N.n_notes + nv_has_draft_row(); }

/* ============ Documents ============ */

static void nv_reset_doc(void) {
    N.text[0] = '\0';
    N.len = N.cur = N.anchor = 0;
    N.dirty = N.save_failed = N.readonly = 0;
    N.scroll = 0.0f;
    N.goal_valid = 0;
    N.layout_dirty = 1;
    N.vl_overflow_logged = 0;
    nv_history_clear();
}

static void nv_new_doc(void) {
    nv_reset_doc();
    nv_strcpy(N.doc_dir, N.folder, (int)sizeof(N.doc_dir));
    N.doc_name[0] = '\0';
    N.auto_name = 1;
}

static int nv_load(const char *dir, const char *name) {
    char path[NXRELAY_PATH_MAX];
    if (nxrelay_join(dir, name, path, (int)sizeof(path)) != NXRELAY_OK) {
        nv_set_status("That file name is not allowed", 1);
        return -1;
    }
    uint64_t total = 0;
    /* Stage in an undo slot: history is reset for the new note anyway, and
     * the clipboard must survive switching notes. */
    char *stage = g_undo[0].text;
    int64_t n = nv_read_file(path, stage, NV_TEXT_MAX, &total);
    if (n < 0) {
        char msg[128];
        nv_strcpy(msg, "Couldn't open the note: ", (int)sizeof(msg));
        nv_strcat(msg, nxrelay_strerror((int)n), (int)sizeof(msg));
        nv_set_status(msg, 1);
        nv_history_clear();
        return -1;
    }
    nv_reset_doc();
    uint32_t len = nv_clean_text(stage, (uint32_t)n, NV_TEXT_MAX);
    nv_move_bytes(N.text, stage, len);
    N.text[len] = '\0';
    N.len = len;
    nv_strcpy(N.doc_dir, dir, (int)sizeof(N.doc_dir));
    nv_strcpy(N.doc_name, name, (int)sizeof(N.doc_name));
    N.auto_name = nv_streq(dir, N.folder);
    if (total > (uint64_t)(NV_TEXT_MAX - 1)) {
        N.readonly = 1;
        nv_set_status("This file is larger than 32 KB, so it opened read-only", 1);
    }
    nv_log("opened", path);
    return 0;
}

/* Save the open document if it has changes. Returns 0 when nothing is
 * left unsaved, -1 when the text is still only in memory. */
static int nv_save(void) {
    if (!N.dirty) return 0;
    if (N.readonly) return -1;
    if (!N.doc_name[0] && N.len == 0) { N.dirty = 0; return 0; }   /* empty draft: nothing to keep */

    char msg[160];
    if (N.auto_name && nv_streq(N.doc_dir, N.folder)) {
        int fr = nv_ensure_folder();
        if (fr != NXRELAY_OK) {
            nv_strcpy(msg, "Couldn't create Documents/Notes: ", (int)sizeof(msg));
            nv_strcat(msg, nxrelay_strerror(fr), (int)sizeof(msg));
            nv_set_status(msg, 1);
            N.save_failed = 1;
            return -1;
        }
    }

    /* Final name: from the title for notes, unchanged for other files. */
    char final_name[NXRELAY_NAME_MAX + 1];
    if (N.auto_name) {
        char stem[NV_TITLE_MAX + 1];
        nv_title_of(N.text, N.len, stem, (int)sizeof(stem));
        if (!stem[0]) {
            if (N.doc_name[0]) nv_stem(N.doc_name, stem, (int)sizeof(stem));
            else nv_strcpy(stem, "Untitled", (int)sizeof(stem));
        }
        const char *ext = N.doc_name[0] && nv_ends_ci(N.doc_name, ".md") ? ".md" : NV_EXT_TXT;
        int ur = nv_unique_name(N.doc_dir, stem, ext, N.doc_name, final_name, (int)sizeof(final_name));
        if (ur != NXRELAY_OK) {
            if (!N.doc_name[0]) {
                nv_strcpy(msg, "Couldn't pick a file name: ", (int)sizeof(msg));
                nv_strcat(msg, nxrelay_strerror(ur), (int)sizeof(msg));
                nv_set_status(msg, 1);
                N.save_failed = 1;
                return -1;
            }
            nv_strcpy(final_name, N.doc_name, (int)sizeof(final_name));   /* keep the old name */
        }
    } else {
        nv_strcpy(final_name, N.doc_name, (int)sizeof(final_name));
    }

    char tmp_name[NXRELAY_NAME_MAX + 1], tmp[NXRELAY_PATH_MAX], dst[NXRELAY_PATH_MAX], old[NXRELAY_PATH_MAX];
    nv_strcpy(tmp_name, ".", (int)sizeof(tmp_name));
    if (nv_strcat(tmp_name, final_name, (int)sizeof(tmp_name)) || nv_strcat(tmp_name, ".saving", (int)sizeof(tmp_name)) ||
        nxrelay_join(N.doc_dir, tmp_name, tmp, (int)sizeof(tmp)) != NXRELAY_OK ||
        nxrelay_join(N.doc_dir, final_name, dst, (int)sizeof(dst)) != NXRELAY_OK) {
        nv_set_status("Couldn't save: the file name is too long", 1);
        N.save_failed = 1;
        return -1;
    }
    old[0] = '\0';
    if (N.doc_name[0] && nxrelay_join(N.doc_dir, N.doc_name, old, (int)sizeof(old)) != NXRELAY_OK) old[0] = '\0';

    /* 1. Write the new text beside the note and check it landed whole. */
    if (nxrelay_stat(tmp, NULL, NULL) == NXRELAY_OK) (void)nx_remove(tmp);   /* our own stale temp */
    int wr = nv_write_file(tmp, N.text, N.len);
    if (wr != NXRELAY_OK) {
        (void)nx_remove(tmp);
        nv_strcpy(msg, "Couldn't save: ", (int)sizeof(msg));
        nv_strcat(msg, wr == NXRELAY_ERR_DENIED ? "this location doesn't allow writing" : "the disk write failed",
                  (int)sizeof(msg));
        nv_set_status(msg, 1);
        N.save_failed = 1;
        return -1;
    }
    /* 2. Replace the old file. */
    if (old[0]) {
        int rr = nx_remove(old);
        if (rr != NX_FS_OK && rr != NX_FS_NOT_FOUND) {
            (void)nx_remove(tmp);
            nv_strcpy(msg, "Couldn't save: ", (int)sizeof(msg));
            nv_strcat(msg, nv_fs_err(rr), (int)sizeof(msg));
            nv_set_status(msg, 1);
            N.save_failed = 1;
            return -1;
        }
    }
    int mv = nx_rename(tmp, dst);
    if (mv != NX_FS_OK) {
        /* The text is safe in the hidden file; the next listing restores it. */
        nv_strcpy(N.doc_name, final_name, (int)sizeof(N.doc_name));
        nv_strcpy(msg, "Saved to a hidden file, but naming it failed: ", (int)sizeof(msg));
        nv_strcat(msg, nv_fs_err(mv), (int)sizeof(msg));
        nv_set_status(msg, 1);
        N.save_failed = 1;
        return -1;
    }
    int renamed = !nv_streq(N.doc_name, final_name);
    nv_strcpy(N.doc_name, final_name, (int)sizeof(N.doc_name));
    N.dirty = 0;
    N.save_failed = 0;
    nxrelay_invalidate(N.doc_dir);
    nv_log("saved", dst);
    if (nv_streq(N.doc_dir, N.folder)) {
        int row = renamed ? -1 : nv_current_row();
        if (row >= 0) nv_preview_of(N.text, N.len, N.previews[row], NV_PREVIEW_MAX);
        else nv_refresh_list();
    }
    return 0;
}

/* Leave the open document: save it first; refuse while it can't be saved. */
static int nv_leave_doc(void) {
    if (nv_save() == 0) return 0;
    if (N.readonly) return 0;    /* nothing can be edited, nothing is lost */
    nv_set_status("Couldn't save this note, so it stays open. Copy its text before closing the window.", 1);
    return -1;
}

static void nv_open_row(int row) {
    int draft = nv_has_draft_row();
    if (row < 0 || row >= nv_row_count()) return;
    if (draft && row == 0) return;                  /* already the open draft */
    int idx = row - draft;
    if (nv_streq(N.doc_dir, N.folder) && nv_streq(N.names[idx], N.doc_name)) return;
    char name[NXRELAY_NAME_MAX + 1];
    nv_strcpy(name, N.names[idx], (int)sizeof(name));
    if (nv_leave_doc() != 0) return;
    if (nv_load(N.folder, name) != 0) nv_refresh_list();
}

static void nv_new_note(void) {
    if (nv_has_draft_row() && N.len == 0) return;   /* the empty draft is already open */
    if (nv_leave_doc() != 0) return;
    nv_new_doc();
    N.list_scroll = 0;
    nv_log("new note", 0);
}

static void nv_step_note(int dir) {
    int row = nv_has_draft_row() ? 0 : nv_current_row();
    if (row < 0) row = dir > 0 ? -1 : nv_row_count();
    nv_open_row(row + dir);
}

/* Move the open note into Notes/Recently Deleted (a rename, not an erase). */
static void nv_trash(void) {
    if (!N.auto_name || !nv_streq(N.doc_dir, N.folder)) {
        nv_set_status("Only notes in Documents/Notes can be deleted from here", 0);
        return;
    }
    if (!N.doc_name[0]) {
        if (N.len == 0) { nv_set_status("This note is empty, so there is nothing to delete", 0); return; }
        if (nv_save() != 0) return;     /* keep a copy, then move it like any note */
    } else if (nv_save() != 0) {
        return;
    }
    char trash[NXRELAY_PATH_MAX], src[NXRELAY_PATH_MAX], dst[NXRELAY_PATH_MAX], stem[NV_TITLE_MAX + 1];
    char dname[NXRELAY_NAME_MAX + 1];
    if (nxrelay_join(N.folder, NV_TRASH_NAME, trash, (int)sizeof(trash)) != NXRELAY_OK ||
        nxrelay_join(N.folder, N.doc_name, src, (int)sizeof(src)) != NXRELAY_OK) {
        nv_set_status("Couldn't delete: the path is too long", 1);
        return;
    }
    if (nv_mkdir_if_missing(trash) != NXRELAY_OK) {
        nv_set_status("Couldn't create the Recently Deleted folder", 1);
        return;
    }
    nv_stem(N.doc_name, stem, (int)sizeof(stem));
    const char *ext = nv_ends_ci(N.doc_name, ".md") ? ".md" : NV_EXT_TXT;
    if (nv_unique_name(trash, stem, ext, "", dname, (int)sizeof(dname)) != NXRELAY_OK ||
        nxrelay_join(trash, dname, dst, (int)sizeof(dst)) != NXRELAY_OK) {
        nv_set_status("Couldn't delete: no free name in Recently Deleted", 1);
        return;
    }
    int rc = nx_rename(src, dst);
    if (rc != NX_FS_OK) {
        char msg[128];
        nv_strcpy(msg, "Couldn't delete: ", (int)sizeof(msg));
        nv_strcat(msg, nv_fs_err(rc), (int)sizeof(msg));
        nv_set_status(msg, 1);
        return;
    }
    nv_log("moved to Recently Deleted:", dst);
    char msg[128];
    nv_strcpy(msg, "Moved \"", (int)sizeof(msg));
    nv_strcat(msg, stem, (int)sizeof(msg));
    nv_strcat(msg, "\" to Recently Deleted", (int)sizeof(msg));
    nxrelay_invalidate(trash);
    nv_refresh_list();
    nv_new_doc();
    if (N.n_notes > 0) nv_load(N.folder, N.names[0]);
    nv_set_status(msg, 0);
}

/* ============ Menu bar (Notes: File Edit View Window Help) ============ */

enum {
    NA_NEW = MENUBAR_APP_ACTION_BASE, NA_SAVE, NA_TRASH,
    NA_UNDO, NA_REDO, NA_CUT, NA_COPY, NA_PASTE, NA_SELECT_ALL,
    NA_SHOW_IN_PATH, NA_RELOAD, NA_PREV, NA_NEXT,
};

/* Standard actions of menubar.c handled by the desktop (desktop_menu_action) */
#define STD_CLOSE_WINDOW 4
#define STD_SHORTCUTS    22
#define STD_ABOUT        23

static const menubar_item_t g_m_file[] = {
    { "New Note",     "Ctrl N", 1, NA_NEW },
    { "Save",         "Ctrl S", 1, NA_SAVE },
    { NULL, NULL, 0, 0 },
    { "Delete Note",  NULL,     1, NA_TRASH },
    { NULL, NULL, 0, 0 },
    { "Close Window", "NX W",   1, STD_CLOSE_WINDOW },
};
static const menubar_item_t g_m_edit[] = {
    { "Undo",       "Ctrl Z",       1, NA_UNDO },
    { "Redo",       "Shift Ctrl Z", 1, NA_REDO },
    { NULL, NULL, 0, 0 },
    { "Cut",        "Ctrl X",       1, NA_CUT },
    { "Copy",       "Ctrl C",       1, NA_COPY },
    { "Paste",      "Ctrl V",       1, NA_PASTE },
    { "Select All", "Ctrl A",       1, NA_SELECT_ALL },
};
static const menubar_item_t g_m_view[] = {
    { "Previous Note",          "Ctrl Up",   1, NA_PREV },
    { "Next Note",              "Ctrl Down", 1, NA_NEXT },
    { NULL, NULL, 0, 0 },
    { "Show Notes Folder in Path", NULL,     1, NA_SHOW_IN_PATH },
    { "Reload List",            "F5",        1, NA_RELOAD },
};
static const menubar_item_t g_m_help[] = {
    { "Keyboard Shortcuts", "Hold NX", 1, STD_SHORTCUTS },
    { "About NeolyxOS",     NULL,      1, STD_ABOUT },
};

#define NITEMS(a) ((int)(sizeof(a) / sizeof(a[0])))
static menubar_menu_t g_notes_menus[5];

static void nv_show_in_path(void) {
    const char *where = N.doc_dir[0] ? N.doc_dir : N.folder;
    if (nv_streq(where, N.folder) && nv_ensure_folder() != NXRELAY_OK) {
        nv_set_status("Couldn't create Documents/Notes", 1);
        return;
    }
    if (path_view_open_at(where) != 0) nv_set_status("Couldn't open Path", 1);
}

static void nv_select_all(void) { N.anchor = 0; N.cur = N.len; N.last_kind = NV_EDIT_NONE; }

static void nv_save_now(void) {
    if (N.readonly) { nv_set_status("This note is read-only", 1); return; }
    if (!N.dirty) { nv_set_status(N.doc_name[0] ? "All changes are saved" : "Type something to save this note", 0); return; }
    N.save_failed = 0;
    if (nv_save() == 0) nv_set_status("Saved", 0);
}

static void notes_menu_action(int menu, int action) {
    (void)menu;
    switch (action) {
        case NA_NEW:          nv_new_note(); break;
        case NA_SAVE:         nv_save_now(); break;
        case NA_TRASH:        nv_trash(); break;
        case NA_UNDO:         nv_undo(); break;
        case NA_REDO:         nv_redo(); break;
        case NA_CUT:          nv_copy(1); break;
        case NA_COPY:         nv_copy(0); break;
        case NA_PASTE:        nv_paste(); break;
        case NA_SELECT_ALL:   nv_select_all(); break;
        case NA_SHOW_IN_PATH: nv_show_in_path(); break;
        case NA_RELOAD:       nv_refresh_list(); break;
        case NA_PREV:         nv_step_note(-1); break;
        case NA_NEXT:         nv_step_note(1); break;
        default:              nv_log("menu action not handled", 0); break;
    }
    nv_ensure_visible();
}

static void nv_register_menus(void) {
    static int done;
    if (done) return;
    const menubar_menu_t *win = menubar_standard_menu("Window");
    if (!win) { nv_log("ERROR: standard Window menu missing; keeping standard menus", 0); return; }
    g_notes_menus[0] = (menubar_menu_t){ "File", g_m_file, NITEMS(g_m_file) };
    g_notes_menus[1] = (menubar_menu_t){ "Edit", g_m_edit, NITEMS(g_m_edit) };
    g_notes_menus[2] = (menubar_menu_t){ "View", g_m_view, NITEMS(g_m_view) };
    g_notes_menus[3] = *win;
    g_notes_menus[4] = (menubar_menu_t){ "Help", g_m_help, NITEMS(g_m_help) };
    if (menubar_register_app_menus("Notes", g_notes_menus, 5, notes_menu_action) == 0) done = 1;
    else nv_log("ERROR: could not register Notes menus", 0);
}

/* ============ Drawing ============ */

static nv_layout_t nv_rects(const desktop_window_t *win) {
    nv_layout_t L;
    nxc_rect_t r = app_view_content_rect(win);
    float sw = NV_SIDEBAR_W;
    if (sw > r.w * 0.45f) sw = r.w * 0.45f;
    L.sidebar   = nxc_rect(r.x, r.y, sw, r.h);
    L.side_head = nxc_rect(r.x, r.y, sw, NV_TOOLBAR_H);
    L.plus      = nxc_rect(r.x + sw - 40.0f, r.y + 8.0f, 28.0f, 28.0f);
    L.side_foot = nxc_rect(r.x, r.y + r.h - NV_STATUS_H, sw, NV_STATUS_H);
    L.list      = nxc_rect(r.x, r.y + NV_TOOLBAR_H, sw, r.h - NV_TOOLBAR_H - NV_STATUS_H);
    L.toolbar   = nxc_rect(r.x + sw, r.y, r.w - sw, NV_TOOLBAR_H);
    L.b_trash   = nxc_rect(r.x + r.w - 40.0f, r.y + 8.0f, 28.0f, 28.0f);
    L.b_path    = nxc_rect(r.x + r.w - 72.0f, r.y + 8.0f, 28.0f, 28.0f);
    L.status    = nxc_rect(r.x + sw, r.y + r.h - NV_STATUS_H, r.w - sw, NV_STATUS_H);
    L.editor    = nxc_rect(r.x + sw, r.y + NV_TOOLBAR_H, r.w - sw, r.h - NV_TOOLBAR_H - NV_STATUS_H);
    L.rows_visible = L.list.h > 0.0f ? (int)(L.list.h / NV_ROW_H) : 1;
    if (L.rows_visible < 1) L.rows_visible = 1;
    return L;
}

static void nv_button(nxc_canvas_t *c, nxc_rect_t r, nxsym_t sym, int hover, int enabled) {
    const nxui_theme_t *th = nxui_theme();
    if (hover && enabled) nxc_fill_rrect(c, r, 7.0f, th->control_fill_hover);
    nxsym_draw(c, sym, r.x + r.w * 0.5f, r.y + r.h * 0.5f, 15.0f, enabled ? th->text_secondary : th->text_tertiary, 0.0f);
}

static void nv_draw_sidebar(nxc_canvas_t *c, const nv_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rect(c, L->sidebar, th->window_titlebar);
    nxc_fill_rect(c, nxc_rect(L->sidebar.x + L->sidebar.w - 1.0f, L->sidebar.y, 1.0f, L->sidebar.h), th->separator);
    nxui_text(c, NXUI_TEXT_TITLE3, L->side_head.x + 16.0f, L->side_head.y + 28.0f, "Notes", th->text_primary, 0);
    nv_button(c, L->plus, NXSYM_PLUS, N.hover_btn == 1, 1);

    int rows = nv_row_count();
    int draft = nv_has_draft_row();
    int cur = draft ? 0 : nv_current_row();
    int max_scroll = rows - L->rows_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (N.list_scroll > max_scroll) N.list_scroll = max_scroll;
    if (N.list_scroll < 0) N.list_scroll = 0;

    if (rows == 0) {
        float cy = L->list.y + L->list.h * 0.38f;
        nxsym_draw(c, NXSYM_DOCUMENT, L->list.x + L->list.w * 0.5f, cy - 26.0f, 28.0f, th->text_tertiary, 0.0f);
        nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(L->list.x, cy, L->list.w, 20.0f), "No notes yet",
                          th->text_secondary, NXC_ALIGN_CENTER);
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(L->list.x + 12.0f, cy + 20.0f, L->list.w - 24.0f, 18.0f),
                          "Press Ctrl N to start one", th->text_tertiary, NXC_ALIGN_CENTER);
    }

    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)L->list.x, (int32_t)L->list.y, (int32_t)L->list.w, (int32_t)L->list.h));
    for (int i = 0; i <= L->rows_visible; i++) {
        int row = N.list_scroll + i;
        if (row >= rows) break;
        float ry = L->list.y + (float)i * NV_ROW_H;
        nxc_rect_t box = nxc_rect(L->list.x + 8.0f, ry + 2.0f, L->list.w - 16.0f, NV_ROW_H - 4.0f);
        int sel = row == cur;
        if (sel) nxc_fill_rrect(c, box, 8.0f, th->selection);
        else if (row == N.hover_row) nxc_fill_rrect(c, box, 8.0f, th->control_fill_hover);
        char title[NV_TITLE_MAX + 1], prev[NV_PREVIEW_MAX];
        if (sel) {
            nv_title_of(N.text, N.len, title, (int)sizeof(title));
            nv_preview_of(N.text, N.len, prev, (int)sizeof(prev));
        } else {
            nv_stem(N.names[row - draft], title, (int)sizeof(title));
            nv_strcpy(prev, N.previews[row - draft], (int)sizeof(prev));
        }
        if (!title[0]) nv_strcpy(title, "New Note", (int)sizeof(title));
        if (!prev[0]) nv_strcpy(prev, "No additional text", (int)sizeof(prev));
        nxui_text_in_rect(c, NXUI_TEXT_BODY_SEMIBOLD, nxc_rect(box.x + 12.0f, box.y + 6.0f, box.w - 24.0f, 20.0f),
                          title, th->text_primary, NXC_ALIGN_LEFT);
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(box.x + 12.0f, box.y + 26.0f, box.w - 24.0f, 18.0f),
                          prev, th->text_secondary, NXC_ALIGN_LEFT);
        if (!sel && row + 1 != cur && row + 1 < rows)
            nxc_fill_rect(c, nxc_rect(box.x + 12.0f, ry + NV_ROW_H - 0.5f, box.w - 24.0f, 1.0f),
                          NXC_WITH_ALPHA(th->separator, 120));
    }
    nxc_restore(c);

    char foot[48], num[12];
    nv_utoa((uint32_t)rows, num, (int)sizeof(num));
    nv_strcpy(foot, num, (int)sizeof(foot));
    nv_strcat(foot, rows == 1 ? " note" : " notes", (int)sizeof(foot));
    if (N.list_truncated) nv_strcat(foot, " (first 128)", (int)sizeof(foot));
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, L->side_foot, foot, th->text_tertiary, NXC_ALIGN_CENTER);
}

static void nv_draw_toolbar(nxc_canvas_t *c, const nv_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rect(c, L->toolbar, th->window_bg);
    nxc_fill_rect(c, nxc_rect(L->toolbar.x, L->toolbar.y + NV_TOOLBAR_H - 1.0f, L->toolbar.w, 1.0f), th->separator);

    /* Save state: dot + word */
    const char *word;
    nxc_color_t dot;
    if (N.readonly)                     { word = "Read only"; dot = th->text_tertiary; }
    else if (N.save_failed)             { word = "Not saved"; dot = th->destructive; }
    else if (N.dirty)                   { word = "Edited";    dot = th->warning; }
    else if (!N.doc_name[0])            { word = "New note";  dot = th->text_tertiary; }
    else                                { word = "Saved";     dot = th->success; }
    float x = L->toolbar.x + NV_PAD_X;
    float cy = L->toolbar.y + NV_TOOLBAR_H * 0.5f;
    nxc_fill_rrect(c, nxc_rect(x, cy - 4.0f, 8.0f, 8.0f), 4.0f, dot);
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(x + 14.0f, L->toolbar.y, 90.0f, NV_TOOLBAR_H), word,
                      th->text_secondary, NXC_ALIGN_LEFT);

    /* Where the note lives */
    const char *where = N.doc_name[0] ? N.doc_name : "not saved yet";
    char loc[NXRELAY_PATH_MAX];
    if (N.auto_name) nv_strcpy(loc, "Documents / Notes", (int)sizeof(loc));
    else nv_strcpy(loc, N.doc_dir, (int)sizeof(loc));
    nv_strcat(loc, "  \xC2\xB7  ", (int)sizeof(loc));
    nv_strcat(loc, where, (int)sizeof(loc));
    float lx = x + 104.0f;
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(lx, L->toolbar.y, L->b_path.x - lx - 8.0f, NV_TOOLBAR_H), loc,
                      th->text_tertiary, NXC_ALIGN_LEFT);

    nv_button(c, L->b_path, NXSYM_FOLDER, N.hover_btn == 2, 1);
    nv_button(c, L->b_trash, NXSYM_TRASH, N.hover_btn == 3, N.auto_name && (N.doc_name[0] || N.len > 0));
}

static void nv_draw_editor(nxc_canvas_t *c, const nv_layout_t *L, int focused) {
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rect(c, L->editor, th->window_bg);
    float tx = L->editor.x + NV_PAD_X, ty = L->editor.y + NV_PAD_Y;
    N.view_h = L->editor.h - 2.0f * NV_PAD_Y;
    nv_relayout(L->editor.w - 2.0f * NV_PAD_X);
    float max_scroll = N.content_h - N.view_h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (N.scroll > max_scroll) N.scroll = max_scroll;

    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)L->editor.x, (int32_t)L->editor.y + 2, (int32_t)L->editor.w, (int32_t)L->editor.h - 4));

    if (N.len == 0) {
        nxui_text(c, NV_STYLE_TITLE, tx, ty + nxui_text_ascent(NV_STYLE_TITLE) + 2.0f, "Title", th->text_tertiary, 0);
        nxui_text(c, NV_STYLE_BODY, tx, ty + nv_lh(1) + 6.0f + nxui_text_ascent(NV_STYLE_BODY) + 2.0f + 8.0f,
                  "Start writing. The first line becomes the note's name.", th->text_tertiary, 0);
    }

    uint32_t sa, sb;
    int has_sel = nv_selection(&sa, &sb);
    int first = nv_vline_of(0);
    /* First visible line: binary search on y */
    {
        int lo = 0, hi = N.vl_n - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (N.vl_y[mid] <= N.scroll) lo = mid; else hi = mid - 1;
        }
        first = lo;
    }
    char buf[NV_DRAW_MAX];
    for (int vi = first; vi < N.vl_n; vi++) {
        float y = ty + N.vl_y[vi] - N.scroll;
        if (y > L->editor.y + L->editor.h) break;
        int title = N.vl_title[vi];
        float lh = nv_lh(title);
        uint32_t s = N.vl_start[vi], e = s + N.vl_len[vi];
        if (has_sel && sb > s && sa <= e) {
            uint32_t a = sa > s ? sa : s, b = sb < e ? sb : e;
            float x0 = nv_x_of(vi, a), x1 = nv_x_of(vi, b);
            if (sb > e && e < N.len && N.text[e] == '\n') x1 += 6.0f;   /* the line break is selected too */
            if (x1 > x0)
                nxc_fill_rect(c, nxc_rect(tx + x0, y + 1.0f, x1 - x0, lh - 2.0f),
                              focused ? NXC_WITH_ALPHA(th->accent, 90) : th->selection);
        }
        uint32_t n = N.vl_len[vi];
        if (n >= NV_DRAW_MAX) n = NV_DRAW_MAX - 1;
        if (n > 0) {
            nv_move_bytes(buf, N.text + s, n);
            buf[n] = '\0';
            nxui_text_style_t st = title ? NV_STYLE_TITLE : NV_STYLE_BODY;
            float asc = nxui_text_ascent(st), desc = nxui_text_descent(st);
            nxui_text(c, st, tx, y + (lh - asc - desc) * 0.5f + asc, buf, th->text_primary, 0);
        }
    }
    /* Caret */
    if (N.vl_n > 0 && !N.readonly) {
        int vi = nv_vline_of(N.cur);
        float lh = nv_lh(N.vl_title[vi]);
        float cx = tx + nv_x_of(vi, N.cur), cyy = ty + N.vl_y[vi] - N.scroll;
        if (focused) {
            if (has_sel || (int)(nxui_time() * 1.8f) % 2 == 0)
                nxc_fill_rect(c, nxc_rect(cx, cyy + 3.0f, 2.0f, lh - 6.0f), th->accent);
        } else {
            nxc_fill_rect(c, nxc_rect(cx, cyy + 3.0f, 1.0f, lh - 6.0f), th->text_tertiary);
        }
    }
    nxc_restore(c);

    /* Errors get a banner across the bottom of the editor so the whole
     * reason is readable; notices use the status bar. */
    if (N.status_err && N.status[0]) {
        nxc_rect_t bn = nxc_rect(L->editor.x + 16.0f, L->editor.y + L->editor.h - 46.0f, L->editor.w - 32.0f, 36.0f);
        nxc_fill_rrect(c, bn, 9.0f, NXC_WITH_ALPHA(th->window_titlebar, 245));
        nxc_fill_rrect(c, bn, 9.0f, NXC_WITH_ALPHA(th->destructive, 46));
        nxc_stroke_rrect(c, bn, 9.0f, 1.0f, NXC_WITH_ALPHA(th->destructive, 150));
        nxsym_draw(c, NXSYM_WARNING, bn.x + 20.0f, bn.y + bn.h * 0.5f, 15.0f, th->destructive, 0.0f);
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(bn.x + 38.0f, bn.y, bn.w - 50.0f, bn.h), N.status,
                          th->text_primary, NXC_ALIGN_LEFT);
    }

    /* Scroll indicator */
    if (max_scroll > 0.0f) {
        float track = L->editor.h - 8.0f;
        float h = track * N.view_h / N.content_h;
        if (h < 24.0f) h = 24.0f;
        float y = L->editor.y + 4.0f + (track - h) * N.scroll / max_scroll;
        nxc_fill_rrect(c, nxc_rect(L->editor.x + L->editor.w - 8.0f, y, 4.0f, h), 2.0f,
                       NXC_WITH_ALPHA(th->text_tertiary, 120));
    }
}

static void nv_draw_status(nxc_canvas_t *c, const nv_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rect(c, L->status, th->window_bg);
    nxc_fill_rect(c, nxc_rect(L->status.x, L->status.y, L->status.w, 1.0f), th->separator);
    /* Ln / Col */
    uint32_t ln = 1, col = 1;
    for (uint32_t i = 0; i < N.cur && i < N.len; i++) {
        if (N.text[i] == '\n') { ln++; col = 1; }
        else if (!nv_is_cont(N.text[i])) col++;
    }
    char left[48], num[12];
    nv_strcpy(left, "Ln ", (int)sizeof(left));
    nv_utoa(ln, num, (int)sizeof(num)); nv_strcat(left, num, (int)sizeof(left));
    nv_strcat(left, ", Col ", (int)sizeof(left));
    nv_utoa(col, num, (int)sizeof(num)); nv_strcat(left, num, (int)sizeof(left));
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(L->status.x + NV_PAD_X, L->status.y, 120.0f, NV_STATUS_H),
                      left, th->text_tertiary, NXC_ALIGN_LEFT);
    /* A notice (errors use the banner) takes the rest of the bar for a few seconds. */
    if (N.status[0] && !N.status_err && nx_gettime() - N.status_ms < NV_STATUS_MS) {
        float mx = L->status.x + NV_PAD_X + 120.0f, mw = L->status.w - 2.0f * NV_PAD_X - 120.0f;
        if (mw > 40.0f)
            nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(mx, L->status.y, mw, NV_STATUS_H), N.status,
                              th->text_secondary, NXC_ALIGN_RIGHT);
        return;
    }
    char right[64];
    nv_utoa(N.words, num, (int)sizeof(num));
    nv_strcpy(right, num, (int)sizeof(right));
    nv_strcat(right, N.words == 1 ? " word  \xC2\xB7  " : " words  \xC2\xB7  ", (int)sizeof(right));
    nv_utoa(N.chars, num, (int)sizeof(num));
    nv_strcat(right, num, (int)sizeof(right));
    nv_strcat(right, N.chars == 1 ? " character" : " characters", (int)sizeof(right));
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(L->status.x + L->status.w - 236.0f - NV_PAD_X + 16.0f, L->status.y,
                      220.0f, NV_STATUS_H), right, th->text_tertiary, NXC_ALIGN_RIGHT);
}

void notes_view_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    nxc_canvas_t *c = nxui_canvas();
    if (!c || !win) return;

    /* Autosave: shortly after the last edit, and when the window loses focus. */
    if (N.dirty && !N.save_failed && !N.readonly) {
        uint64_t now = nx_gettime();
        if (!win->focused || now - N.edit_ms >= NV_AUTOSAVE_MS) (void)nv_save();
    }
    if (N.status_err && !N.save_failed && !N.dirty && N.status[0] && nx_gettime() - N.status_ms >= NV_STATUS_MS * 2)
        N.status[0] = '\0';

    nv_layout_t L = nv_rects(win);
    nv_draw_sidebar(c, &L);
    nv_draw_toolbar(c, &L);
    nv_draw_editor(c, &L, win->focused);
    nv_draw_status(c, &L);
}

/* ============ Input ============ */

static char nv_sc_ascii(uint8_t sc, int shift) {
    static const char lo[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./";
    static const char hi[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?";
    if (sc == 0x39) return ' ';
    if (sc >= sizeof(lo) - 1) return 0;
    return shift ? hi[sc] : lo[sc];
}

void notes_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx) {
    (void)ctx;
    if (!win) return;
    if (sc == 0x2A || sc == 0x36) { N.shift = pressed ? 1 : 0; return; }
    if (sc == 0x1D) { N.ctrl = pressed ? 1 : 0; return; }
    if (sc == 0x38) { N.alt = pressed ? 1 : 0; return; }
    if (!pressed) return;
    int sh = N.shift;
    uint32_t a, b;
    int sel = nv_selection(&a, &b);

    if (N.ctrl) {
        switch (sc) {
            case 0x31: nv_new_note(); break;                                  /* N */
            case 0x1F: nv_save_now(); break;                                  /* S */
            case 0x2C: if (sh) nv_redo(); else nv_undo(); break;              /* Z */
            case 0x15: nv_redo(); break;                                      /* Y */
            case 0x1E: nv_select_all(); break;                                /* A */
            case 0x2E: nv_copy(0); break;                                     /* C */
            case 0x2D: nv_copy(1); break;                                     /* X */
            case 0x2F: nv_paste(); break;                                     /* V */
            case 0x4B: nv_move(nv_word_left(N.cur), sh); N.goal_valid = 0; break;
            case 0x4D: nv_move(nv_word_right(N.cur), sh); N.goal_valid = 0; break;
            case 0x47: nv_move(0, sh); N.goal_valid = 0; break;               /* Home */
            case 0x4F: nv_move(N.len, sh); N.goal_valid = 0; break;           /* End */
            case 0x48: nv_step_note(-1); return;                              /* Up */
            case 0x50: nv_step_note(1); return;                               /* Down */
            case 0x0E: nv_backspace(1); break;                                /* Backspace: word */
            default: return;
        }
        nv_relayout(N.layout_w);
        nv_ensure_visible();
        return;
    }
    if (N.alt) return;

    int page = N.view_h > 0.0f ? (int)(N.view_h / nv_lh(0)) - 1 : 10;
    if (page < 1) page = 1;
    switch (sc) {
        case 0x1C: nv_insert("\n", 1, NV_EDIT_OTHER); break;                  /* Return */
        case 0x0E: nv_backspace(0); break;
        case 0x53: nv_delete_fwd(); break;                                    /* Delete */
        case 0x0F: nv_insert("    ", 4, NV_EDIT_TYPE); break;                 /* Tab */
        case 0x01: N.anchor = N.cur; return;                                  /* Esc */
        case 0x3F: nv_refresh_list(); return;                                 /* F5 */
        case 0x4B:                                                            /* Left */
            if (sel && !sh) nv_move(a, 0); else nv_move(nv_prev(N.cur), sh);
            N.goal_valid = 0;
            break;
        case 0x4D:                                                            /* Right */
            if (sel && !sh) nv_move(b, 0); else nv_move(nv_next(N.cur), sh);
            N.goal_valid = 0;
            break;
        case 0x48: nv_move_vert(-1, sh); break;                               /* Up */
        case 0x50: nv_move_vert(1, sh); break;                                /* Down */
        case 0x49: nv_move_vert(-page, sh); break;                            /* PgUp */
        case 0x51: nv_move_vert(page, sh); break;                             /* PgDn */
        case 0x47: nv_relayout(N.layout_w); nv_move(N.vl_start[nv_vline_of(N.cur)], sh); N.goal_valid = 0; break;
        case 0x4F: nv_relayout(N.layout_w); nv_move(nv_line_end(nv_vline_of(N.cur)), sh); N.goal_valid = 0; break;
        default: {
            char ch = desktop_key_ascii();
            if ((uint8_t)ch < 0x20 || (uint8_t)ch > 0x7E) ch = nv_sc_ascii(sc, sh);
            if (!ch) return;
            nv_insert(&ch, 1, ch == ' ' ? NV_EDIT_OTHER : NV_EDIT_TYPE);
            break;
        }
    }
    nv_relayout(N.layout_w);
    nv_ensure_visible();
}

static uint32_t nv_pos_at_point(const nv_layout_t *L, float x, float y) {
    nv_relayout(L->editor.w - 2.0f * NV_PAD_X);
    float ly = y - (L->editor.y + NV_PAD_Y) + N.scroll;
    if (ly < 0.0f) return N.vl_n > 0 ? nv_pos_at_x(0, x - (L->editor.x + NV_PAD_X)) : 0;
    int lo = 0, hi = N.vl_n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (N.vl_y[mid] <= ly) lo = mid; else hi = mid - 1;
    }
    if (ly > N.content_h) return N.len;
    return nv_pos_at_x(lo, x - (L->editor.x + NV_PAD_X));
}

void notes_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)ctx;
    if (!win) return;
    nv_layout_t L = nv_rects(win);
    float x = (float)win->x + (float)lx, y = (float)win->y + (float)APP_VIEW_TITLE_H + (float)ly;
    int press = (b & 1) && !(pb & 1);
    int held = (b & 1) && (pb & 1);

    if (held && N.dragging) {
        /* Drag-select; past the top or bottom edge scrolls */
        if (y < L.editor.y + 6.0f) N.scroll -= 12.0f;
        else if (y > L.editor.y + L.editor.h - 6.0f) N.scroll += 12.0f;
        if (N.scroll < 0.0f) N.scroll = 0.0f;
        N.cur = nv_pos_at_point(&L, x, y);
        N.goal_valid = 0;
        return;
    }
    if (!(b & 1)) N.dragging = 0;

    N.hover_btn = nv_in(L.plus, x, y) ? 1 : nv_in(L.b_path, x, y) ? 2 : nv_in(L.b_trash, x, y) ? 3 : 0;
    N.hover_row = -1;
    if (nv_in(L.list, x, y)) {
        int r = N.list_scroll + (int)((y - L.list.y) / NV_ROW_H);
        if (r >= 0 && r < nv_row_count()) N.hover_row = r;
    }
    if (!press) return;

    switch (N.hover_btn) {
        case 1: nv_new_note(); return;
        case 2: nv_show_in_path(); return;
        case 3: nv_trash(); return;
        default: break;
    }
    if (nv_in(L.list, x, y)) {
        if (x >= L.list.x + L.list.w - 14.0f && nv_row_count() > L.rows_visible) {
            N.list_scroll += y < L.list.y + L.list.h * 0.5f ? -L.rows_visible : L.rows_visible;
            return;
        }
        if (N.hover_row >= 0) { nv_open_row(N.hover_row); N.scroll = 0.0f; }
        return;
    }
    if (nv_in(L.editor, x, y)) {
        if (x >= L.editor.x + L.editor.w - 12.0f && N.content_h > N.view_h) {
            N.scroll += (y < L.editor.y + L.editor.h * 0.5f ? -1.0f : 1.0f) * N.view_h * 0.9f;
            if (N.scroll < 0.0f) N.scroll = 0.0f;
            return;
        }
        uint32_t pos = nv_pos_at_point(&L, x, y);
        uint64_t now = nx_gettime();
        if (pos == N.last_click_pos && now - N.last_click_ms < NV_DBL_MS) {
            /* Double-click: select the word under the pointer */
            uint32_t s = pos, e = pos;
            while (s > 0 && N.text[s - 1] != ' ' && N.text[s - 1] != '\n') s--;
            while (e < N.len && N.text[e] != ' ' && N.text[e] != '\n') e++;
            N.anchor = s;
            N.cur = e;
            N.last_click_ms = 0;
            N.dragging = 0;
            return;
        }
        nv_move(pos, N.shift);
        N.goal_valid = 0;
        N.dragging = 1;
        N.last_click_pos = pos;
        N.last_click_ms = now;
    }
}

/* ============ Lifecycle ============ */

static int nv_window_open(void) {
    if (N.win_id == 0) return 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (w && w->id == N.win_id) return 1;
    }
    return 0;
}

/* Open a file given as a full path (from Path). */
static int nv_open_path(const char *canon) {
    char dir[NXRELAY_PATH_MAX];
    if (nxrelay_parent(canon, dir, (int)sizeof(dir)) != NXRELAY_OK) return -1;
    const char *name = nxrelay_basename(canon);
    if (nv_streq(dir, N.doc_dir) && nv_streq(name, N.doc_name)) return 0;   /* already open */
    if (nv_leave_doc() != 0) return -1;
    return nv_load(dir, name);
}

int notes_view_open(uint32_t win_id) {
    if (win_id == 0) return -1;
    /* One Notes window: a second launch focuses the first. */
    if (N.magic == NV_MAGIC && nv_window_open() && N.win_id != win_id) {
        nv_log("already open; focusing the existing window", 0);
        if (g_pending_open[0]) { (void)nv_open_path(g_pending_open); g_pending_open[0] = '\0'; }
        desktop_activate_window(N.win_id);
        return -1;
    }
    if (nxrelay_selftest() != 0) {
        nv_log("relay self-test failed; refusing to open", 0);
        return -1;
    }
    char folder[NXRELAY_PATH_MAX], docs[NXRELAY_PATH_MAX];
    if (nxrelay_location(NXRELAY_LOC_DOCUMENTS, docs, (int)sizeof(docs)) != NXRELAY_OK ||
        nxrelay_join(docs, "Notes", folder, (int)sizeof(folder)) != NXRELAY_OK) {
        nv_log("cannot resolve the notes folder", 0);
        return -1;
    }

    if (N.magic != NV_MAGIC) {
        /* First open since boot: start clean. */
        uint8_t *raw = (uint8_t *)&N;
        for (uint32_t i = 0; i < sizeof(N); i++) raw[i] = 0;
        g_cw_ready = 0;
        nv_strcpy(N.folder, folder, (int)sizeof(N.folder));
        nv_refresh_list();
        if (N.n_notes > 0 && nv_load(N.folder, N.names[0]) == 0) { /* opened the first note */ }
        else nv_new_doc();
        N.magic = NV_MAGIC;
    } else {
        /* Reopened: keep the note that was open (and any unsaved text). */
        if (!nv_streq(N.folder, folder) && !N.dirty) {
            nv_strcpy(N.folder, folder, (int)sizeof(N.folder));
            nv_new_doc();
        }
        if (N.dirty) { N.save_failed = 0; (void)nv_save(); }
        nv_refresh_list();
    }
    N.win_id = win_id;
    N.shift = N.ctrl = N.alt = N.dragging = 0;
    N.hover_row = -1;
    N.hover_btn = 0;
    N.layout_dirty = 1;
    nv_register_menus();
    if (g_pending_open[0]) {
        if (nv_open_path(g_pending_open) != 0) nv_log("could not open", g_pending_open);
        g_pending_open[0] = '\0';
    }
    nv_log("opened window; notes folder", N.folder);
    return 0;
}

int notes_view_open_file(const char *path) {
    char canon[NXRELAY_PATH_MAX];
    if (!path || nxrelay_normalize(path, canon, (int)sizeof(canon)) != NXRELAY_OK) {
        nv_log("open_file: rejected", path ? path : "(null)");
        return -1;
    }
    if (N.magic == NV_MAGIC && nv_window_open()) {
        int rc = nv_open_path(canon);
        (void)desktop_activate_window(N.win_id);
        return rc;
    }
    nv_strcpy(g_pending_open, canon, (int)sizeof(g_pending_open));
    if (desktop_launch_app("Notes") != 0) {
        g_pending_open[0] = '\0';
        return -1;
    }
    return 0;
}
