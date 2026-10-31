/*
 * NeolyxOS - System Settings: internal interfaces (Settings area only)
 *
 * Shared between the files of the Settings app view:
 *   settings_view.c          window, sidebar, search, navigation
 *   settings_ui.c            immediate-mode form layout for panes ("sui")
 *   settings_prefs.c         validated, audited access to the .nlc config
 *   settings_dock_adapter.c  the only file that talks to dock.h
 *   settings_sysinfo.c       read-only system information clients
 *   settings_panes_*.c       the panes
 *
 * Nothing outside shell/views/settings_*.c may include this header.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef SETTINGS_INTERNAL_H
#define SETTINGS_INTERNAL_H

#include <stdint.h>
#include "nxcanvas.h"
#include "nxui.h"
#include "nxglass.h"
#include "settings_pane.h"

/* ============ Logging ============ */

/* Writes "[SETTINGS] " + msg (+ optional value) + newline to the debug log. */
void settings_log(const char *msg);
void settings_log2(const char *msg, const char *value);

/* ============ Small freestanding string helpers ============ */

int  sx_strlen(const char *s);
void sx_strcpy(char *dst, const char *src, int cap);           /* always NUL-terminates */
void sx_strcat(char *dst, const char *src, int cap);
int  sx_streq(const char *a, const char *b);
int  sx_streq_ci(const char *a, const char *b);
int  sx_contains_ci(const char *hay, const char *needle);       /* "" matches */
/* Decimal formatting; returns dst. */
char *sx_itoa(int64_t v, char *dst, int cap);
/* "a" + "b" + "c" into dst (NULL parts skipped). */
void sx_join3(char *dst, int cap, const char *a, const char *b, const char *c);

/* ============ Navigation (settings_view.c) ============ */

/* Go to a pane by id inside the open Settings window (pushes history).
 * Returns 0, or -1 if the id is unknown. */
int settings_navigate(const char *pane_id);
/* Focus an app window by name if open, otherwise launch it. Returns 0 on success. */
int settings_open_app(const char *name);
/* Sidebar row height preference (Appearance > Sidebar icon size): 0,1,2. */
int  settings_sidebar_size(void);
void settings_set_sidebar_size(int size);

/* ============ Pane table metadata ============ */

typedef enum {
    STILE_SYM = 0,     /* NXSYM glyph */
    STILE_TEXT,        /* short text glyph ("Aa") */
    STILE_MOUSE,       /* drawn mouse outline */
    STILE_UPDATE       /* drawn download arrow in a ring */
} settings_tile_kind_t;

typedef struct {
    settings_tile_kind_t kind;
    int                  sym;      /* nxsym_t for STILE_SYM */
    const char          *text;     /* for STILE_TEXT */
    nxc_color_t          color;    /* tile fill */
} settings_tile_t;

/* Draw an icon tile (rounded square, white glyph) centered at cx, cy. */
void settings_draw_tile(nxc_canvas_t *c, const settings_tile_t *t, float cx, float cy, float size);

/* Draw only the glyph of a tile (no tile fill) in color, centered at cx, cy;
 * size is the tile size the glyph is proportioned to (rails use this). */
void settings_draw_glyph(nxc_canvas_t *c, const settings_tile_t *t, float cx, float cy, float size,
                         nxc_color_t color);

/* Tile of a pane by id (never NULL; a neutral tile if unknown). */
const settings_tile_t *settings_tile_for(const char *pane_id);

/* ============ Immediate-mode pane UI ("sui") ============ */

/*
 * A pane written against sui is a single build function that lays out
 * its content top to bottom. The same function runs in three passes:
 *   MEASURE  computes card heights and the content height (no drawing)
 *   DRAW     draws into the detail area (absolute coordinates, clipped)
 *   EVENT    hit-tests one pointer event against the same layout
 * Controls return non-zero when the user changed them in an EVENT pass,
 * so a pane reads like:  if (sui_toggle(ui, "Night Mode", NULL, &on)) apply(on);
 * Layout is deterministic, so the three passes agree on every rect.
 */

typedef enum { SUI_MEASURE = 0, SUI_DRAW, SUI_EVENT } sui_pass_t;

#define SUI_MAX_CARDS 24

typedef struct sui {
    sui_pass_t     pass;
    nxc_canvas_t  *c;
    nxc_rect_t     area;          /* detail area, absolute */
    float          x, w;          /* content column */
    float          y;             /* layout cursor (absolute, scroll applied) */
    float          top;           /* area.y + padding - scroll */
    int            ctl;           /* control counter (ids) */
    int            card;          /* card counter */
    int            in_card;
    int            row_in_card;
    float          card_y;
    /* pointer (absolute) for this pass */
    int32_t        mx, my;
    uint8_t        press, release, held;
} sui_t;

typedef void (*sui_build_fn)(sui_t *ui);

/* Pane glue: implement settings_pane_t on top of a build function. */
void sui_pane_render(sui_build_fn fn, nxc_canvas_t *c, nxc_rect_t area);
int  sui_pane_mouse(sui_build_fn fn, int32_t lx, int32_t ly, uint8_t b, uint8_t pb);
int  sui_pane_key(sui_build_fn fn, uint8_t sc, uint8_t pressed, char ascii);
void sui_pane_reset(void);              /* scroll to top, clear interaction */
float sui_scroll_offset(void);          /* current scroll (px) of the visible sui pane */
void sui_pointer_left(void);            /* pointer moved out of the pane: drop hover */

#define SUI_DEFINE_PANE(sym_, id_, title_, build_)                                       \
    static void sym_##_render(nxc_canvas_t *c, nxc_rect_t a) { sui_pane_render(build_, c, a); } \
    static int  sym_##_mouse(int32_t x, int32_t y, uint8_t b, uint8_t pb) {              \
        return sui_pane_mouse(build_, x, y, b, pb); }                                    \
    static int  sym_##_key(uint8_t sc, uint8_t p, char ch) { return sui_pane_key(build_, sc, p, ch); } \
    const settings_pane_t sym_ = { id_, title_, sym_##_render, sym_##_mouse, sym_##_key, sui_pane_reset }

/* Layout */
void sui_header(sui_t *ui, const char *pane_id, const char *title, const char *subtitle);
void sui_section(sui_t *ui, const char *title);          /* small header above a card */
void sui_card_begin(sui_t *ui);
void sui_card_end(sui_t *ui);
void sui_note(sui_t *ui, const char *text);              /* wrapped footnote under a card */
void sui_space(sui_t *ui, float h);

/* Rows (inside a card) */
void sui_info(sui_t *ui, const char *label, const char *value);
int  sui_toggle(sui_t *ui, const char *label, const char *detail, int *value);
/* Returns 1 while dragging (value changed), 2 when the drag ends (commit). */
int  sui_slider(sui_t *ui, const char *label, float *value, const char *lo, const char *hi);
int  sui_segmented(sui_t *ui, const char *label, const char *const *labels, int count, int *selected);
int  sui_button_row(sui_t *ui, const char *label, const char *detail, const char *button,
                    nxglass_button_kind_t kind, int enabled);
int  sui_nav_row(sui_t *ui, const char *pane_id, const char *label, const char *detail);
int  sui_swatches(sui_t *ui, const char *label, const nxc_color_t *colors, const char *const *names,
                  int count, int *selected);
/* Usage bar (0..1) with a caption; value < 0 shows an "unavailable" bar. */
void sui_meter(sui_t *ui, const char *label, const char *caption, float value);
/* Free-form block of height h inside a card: returns its rect; draw only when
 * ui->pass == SUI_DRAW. */
nxc_rect_t sui_block(sui_t *ui, float h);
/* A custom clickable rect (inside a sui_block): fills *state with
 * NXGLASS_HOVER / NXGLASS_PRESSED for drawing; returns 1 when clicked. */
int  sui_hotspot(sui_t *ui, nxc_rect_t r, uint32_t *state);
/* Status line with a colored badge. */
void sui_status(sui_t *ui, const char *label, const char *badge, nxc_color_t color);

/* Word-wrapped text; returns the height used. Draws only when draw != 0. */
float sui_wrap_text(nxc_canvas_t *c, nxui_text_style_t style, float x, float y, float w,
                    const char *text, nxc_color_t color, int draw);

/* ============ Preferences (settings_prefs.c) ============ */

/*
 * Every preference lives in the user .nlc config (NXC_USER_DEFAULTS),
 * in plain sections ("appearance", "display", "mouse", ...). Names and
 * values are validated; every change is written to the audit log as
 *   [SETTINGS] config <section>.<key>: <old> -> <new>
 */
int  sprefs_init(void);                         /* 0 ok, -1: in-memory only */
int  sprefs_get_int(const char *sec, const char *key, int def, int min, int max);
int  sprefs_get_bool(const char *sec, const char *key, int def);
/* Returns def unless the stored value is one of allowed[0..n-1]. */
int  sprefs_get_choice(const char *sec, const char *key, const char *const *allowed, int n, int def);
int  sprefs_set_int(const char *sec, const char *key, int value, int min, int max);
int  sprefs_set_bool(const char *sec, const char *key, int value);
int  sprefs_set_choice(const char *sec, const char *key, const char *const *allowed, int n, int index);
/* 1 if the backing config could be loaded (changes are kept for the session). */
int  sprefs_available(void);

/* ============ Dock adapter (settings_dock_adapter.c) ============ */

typedef struct {
    int   available;               /* dock state reachable */
    int   icon_size;               /* 32..80 px */
    int   magnification;           /* 0/1 */
    float magnification_amount;    /* 1.0..2.0 */
    int   autohide;                /* 0/1 */
    int   position;                /* 0 bottom, 1 left, 2 right */
    int   position_supported;      /* dock renders at that position */
    int   layout;                  /* 0 dock, 1 taskbar */
    int   app_count;               /* icons in the dock */
} settings_dock_info_t;

int settings_dock_read(settings_dock_info_t *out);
int settings_dock_set_icon_size(int px);
int settings_dock_set_magnification(int on, float amount);
int settings_dock_set_autohide(int on);
int settings_dock_set_position(int position);
int settings_dock_set_layout(int layout);

/* ============ System information (settings_sysinfo.c) ============ */

#define SETTINGS_ERR_UNSUPPORTED (-38)   /* no kernel interface for this yet */

int settings_sys_cpu_brand(char *out, int cap);        /* 0 or error */
int settings_sys_hypervisor(char *out, int cap);       /* 0 = VM (name), 1 = bare metal, <0 error */
int settings_sys_display(uint32_t *w, uint32_t *h, uint32_t *bpp);
uint64_t settings_sys_uptime_ms(void);
int settings_sys_clock(int *year, int *month, int *day, int *hour, int *min, int *sec, int *wday);
/* Memory comes from SYS_INFO. Volume statistics need a kernel interface
 * that does not exist yet (docs/desktop/proposals/settings.md) and return
 * SETTINGS_ERR_UNSUPPORTED. The host preview (no SYS_INFO, no volumes)
 * returns clearly marked demo data. */
int settings_sys_memory(uint64_t *total_bytes, uint64_t *used_bytes);
int settings_sys_volume(const char **name, uint64_t *total_bytes, uint64_t *used_bytes);
/* CPU topology from CPUID: logical processors and cores per package.
 * Either may be 0 when the processor does not report it. */
int settings_sys_cpu_topology(uint32_t *logical, uint32_t *cores);
/* Notable instruction-set extensions from CPUID ("SSE4.2, AVX2, AES"). */
int settings_sys_cpu_features(char *out, int cap);
/* Kernel identity and counters from SYS_INFO (50) and abi.query (0).
 * Strings are always NUL-terminated; fields the kernel does not report
 * are zero / empty. Returns 0 when SYS_INFO answered. */
typedef struct {
    char     os_name[32];
    char     os_version[32];
    char     arch[16];
    uint64_t uptime_ms;
    uint32_t num_cpus;
    int      abi_ok;               /* abi.query answered */
    uint16_t abi_major, abi_minor, abi_patch;
    uint32_t syscall_count;
} settings_kernel_info_t;
int settings_sys_kernel(settings_kernel_info_t *out);
/* Human-readable size ("12.4 GB"); decimal = 1 uses powers of 1000 (storage). */
void settings_format_bytes(uint64_t bytes, int decimal, char *out, int cap);

/* ============ Panes ============ */

extern const settings_pane_t settings_pane_about;
extern const settings_pane_t settings_pane_general;
extern const settings_pane_t settings_pane_appearance;
extern const settings_pane_t settings_pane_display;
extern const settings_pane_t settings_pane_wallpaper;
extern const settings_pane_t settings_pane_dock;
extern const settings_pane_t settings_pane_sound;
extern const settings_pane_t settings_pane_keyboard;
extern const settings_pane_t settings_pane_mouse;
extern const settings_pane_t settings_pane_notifications;
extern const settings_pane_t settings_pane_privacy;
extern const settings_pane_t settings_pane_users;
extern const settings_pane_t settings_pane_datetime;
extern const settings_pane_t settings_pane_storage;
extern const settings_pane_t settings_pane_fonts;
extern const settings_pane_t settings_pane_update;
extern const settings_pane_t settings_pane_developer;

/* Accent color preference (no NXUI accent API yet; Settings keeps it). */
nxc_color_t settings_accent_color(void);

#endif /* SETTINGS_INTERNAL_H */
