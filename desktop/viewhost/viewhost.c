/*
 * NeolyxOS - view host (see viewhost.h)
 *
 * Event loop:
 *   wait for a window event (or the next redraw deadline)
 *   -> key / pointer / wheel / configure / close into the view's callbacks
 *   -> when something changed: begin a frame, draw the view into the app
 *      backbuffer, copy it into the window buffer, commit
 * A commit makes the buffer busy until FRAME_DONE; changes in between are
 * kept (g_host.dirty) and drawn at the next FRAME event.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "viewhost.h"
#include "app_view.h"
#include "nxui.h"
#include "nxsyscall.h"
#include "nxwindow.h"

/* A view may ask to be drawn again later (animations, a listing that is
 * still loading); without any request the host still redraws once a
 * second so a view that polls (the Path relay) stays current. */
#define VH_IDLE_REDRAW_MS   1000u
#define VH_MIN_FRAME_MS     16u

static struct {
    const viewhost_app_t *app;
    nxw_window_t   *win;
    desktop_window_t dw;            /* what the view sees as its window */
    uint32_t        max_w, max_h;   /* backbuffer size */
    int             dirty;
    int             busy;           /* waiting for FRAME_DONE */
    uint64_t        redraw_at;      /* 0: none requested */
    uint64_t        last_draw_ms;
    uint8_t         buttons;        /* pointer buttons held, NXW_BUTTON_* */
    char            key_ascii;
    uint8_t         key_mods;
    int32_t         ptr_x, ptr_y;
    int             running;
} g_host;

static void vh_log(const char *a, const char *b) {
    nx_debug_print("[VIEWHOST] ");
    if (g_host.app && g_host.app->title) { nx_debug_print(g_host.app->title); nx_debug_print(": "); }
    if (a) nx_debug_print(a);
    if (b) { nx_debug_print(" "); nx_debug_print(b); }
    nx_debug_print("\n");
}

static void vh_log_code(const char *what, int code) {
    char num[16];
    int n = 0;
    uint32_t v = code < 0 ? (uint32_t)(-(int64_t)code) : (uint32_t)code;
    char tmp[12];
    int t = 0;
    do { tmp[t++] = (char)('0' + v % 10u); v /= 10u; } while (v && t < 11);
    if (code < 0) num[n++] = '-';
    while (t > 0) num[n++] = tmp[--t];
    num[n] = '\0';
    vh_log(what, num);
}

static void vh_copy(char *d, const char *s, int cap) {
    int i = 0;
    if (!d || cap <= 0) return;
    if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = '\0';
}

/* ============ What the view sees of the shell ============ */
/* The view draws in "screen" coordinates with its content at
 * (win->x, win->y + APP_VIEW_TITLE_H); the host puts that at (0, 0). */

const desktop_window_t *desktop_window_slot(int i) {
    if (i == 0 && g_host.dw.id != 0) return &g_host.dw;
    return 0;
}

uint32_t desktop_focused_window_id(void) {
    return g_host.dw.focused ? g_host.dw.id : 0;
}

int desktop_activate_window(uint32_t window_id) {
    /* The shell decides focus; a hosted view can only redraw. */
    if (window_id != g_host.dw.id || window_id == 0) return -1;
    g_host.dirty = 1;
    return 0;
}

void desktop_set_window_place(uint32_t window_id, const char *place) {
    /* The window protocol has one title; the place stays inside the view. */
    if (window_id != g_host.dw.id) return;
    vh_copy(g_host.dw.place, place, (int)sizeof(g_host.dw.place));
}

int desktop_set_window_title(uint32_t window_id, const char *title) {
    if (window_id != g_host.dw.id || !title || !g_host.win) return -1;
    int r = nxw_window_set_title(g_host.win, title);
    if (r != 0) { vh_log_code("set title failed:", r); return -1; }
    vh_copy(g_host.dw.title, title, (int)sizeof(g_host.dw.title));
    return 0;
}

void desktop_damage_window_content(uint32_t window_id, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    (void)x; (void)y; (void)w; (void)h;
    if (window_id == g_host.dw.id) g_host.dirty = 1;
}

void desktop_window_redraw_in(uint32_t window_id, uint32_t delay_ms) {
    if (window_id != g_host.dw.id) return;
    uint64_t at = nx_gettime() + delay_ms;
    if (g_host.redraw_at == 0 || at < g_host.redraw_at) g_host.redraw_at = at;
}

char desktop_key_ascii(void) { return g_host.key_ascii; }

uint8_t desktop_key_modifiers(void) {
    /* Same bits as the shell's (desktop_shell.h): shift 1, ctrl 2, alt 4, NX 8 */
    return g_host.key_mods;
}

uint8_t desktop_mouse_buttons(void) {
    uint8_t b = 0;
    if (g_host.buttons & NXW_BUTTON_LEFT)   b |= 0x1;
    if (g_host.buttons & NXW_BUTTON_RIGHT)  b |= 0x2;
    if (g_host.buttons & NXW_BUTTON_MIDDLE) b |= 0x4;
    return b;
}

/* Launching another app is the shell's job; a hosted view asks it through
 * the window protocol (viewhost_shell.c) instead. */
int desktop_launch_app(const char *name) {
    vh_log("cannot launch apps from a hosted view:", name ? name : "(null)");
    return -1;
}

int viewhost_shell_open(const char *path) {
    if (!g_host.win) return -1;
    int r = nxw_shell_open(g_host.win, path);
    if (r != 0) { vh_log_code("shell open not sent, code", r); return -1; }
    vh_log("asked the shell to open", path);
    return 0;
}

/* ============ Drawing ============ */

static void vh_draw(void) {
    if (!g_host.win || g_host.busy) return;
    nxw_frame_t f;
    int r = nxw_frame_begin(g_host.win, &f);
    if (r == -NXW_EBUSY) { g_host.busy = 1; return; }
    if (r != 0) { vh_log_code("frame_begin failed:", r); g_host.running = 0; return; }

    uint32_t w = f.width, h = f.height;
    if (w > g_host.max_w) w = g_host.max_w;
    if (h > g_host.max_h) h = g_host.max_h;
    g_host.dw.width = w;
    g_host.dw.height = h;

    nxui_pass_begin(0, 0, (int32_t)w, (int32_t)h, 1);
    if (g_host.app->render) g_host.app->render(&g_host.dw, g_host.dw.render_ctx);

    /* Backbuffer rows are max_w pixels; the window buffer has its own stride. */
    nxc_canvas_t *c = nxui_canvas();
    if (!c || !c->target || !c->target->pixels) { vh_log("no backbuffer", 0); g_host.running = 0; return; }
    const uint32_t *src = c->target->pixels;
    uint32_t src_stride = (uint32_t)c->target->stride;   /* pixels */
    uint32_t dst_stride = f.stride / 4u;
    for (uint32_t y = 0; y < h; y++) {
        const uint32_t *s = src + (uint64_t)y * src_stride;
        uint32_t *d = f.pixels + (uint64_t)y * dst_stride;
        for (uint32_t x = 0; x < w; x++) d[x] = s[x] | 0xFF000000u;
    }
    r = nxw_frame_commit(g_host.win, 0, 0);
    if (r != 0) { vh_log_code("commit failed:", r); g_host.running = 0; return; }
    g_host.busy = 1;
    g_host.dirty = 0;
    g_host.last_draw_ms = nx_gettime();
}

/* ============ Events ============ */

static void vh_pointer(const nxw_event_t *ev) {
    int32_t lx = ev->pointer.x, ly = ev->pointer.y;
    g_host.ptr_x = lx;
    g_host.ptr_y = ly;
    switch (ev->pointer.kind) {
    case NXW_PTR_WHEEL:
        if (g_host.app->scroll && ev->pointer.wheel != 0)
            g_host.app->scroll(&g_host.dw, lx, ly, ev->pointer.wheel, g_host.dw.scroll_ctx);
        break;
    case NXW_PTR_MOTION:
    case NXW_PTR_BUTTON:
    case NXW_PTR_ENTER: {
        uint8_t prev = desktop_mouse_buttons();
        g_host.buttons = (uint8_t)(ev->pointer.buttons & 0x7u);
        if (g_host.app->mouse)
            g_host.app->mouse(&g_host.dw, lx, ly, desktop_mouse_buttons(), prev, g_host.dw.mouse_ctx);
        break;
    }
    case NXW_PTR_LEAVE: {
        /* Outside the content: report a far-away pointer so hover clears */
        uint8_t prev = desktop_mouse_buttons();
        g_host.buttons = 0;
        if (g_host.app->mouse)
            g_host.app->mouse(&g_host.dw, -10000, -10000, 0, prev, g_host.dw.mouse_ctx);
        break;
    }
    default:
        return;
    }
    g_host.dirty = 1;
}

static void vh_event(const nxw_event_t *ev) {
    switch (ev->type) {
    case NXW_EV_CONFIGURE:
        g_host.dw.focused = (ev->configure.state & NXW_STATE_ACTIVE) ? 1 : 0;
        g_host.dirty = 1;
        break;
    case NXW_EV_CLOSE:
        vh_log("closing", 0);
        g_host.running = 0;
        break;
    case NXW_EV_KEY:
        g_host.key_ascii = (char)ev->key.ascii;
        g_host.key_mods = ev->key.modifiers;
        if (g_host.app->key)
            g_host.app->key(&g_host.dw, ev->key.scancode, ev->key.pressed, g_host.dw.key_ctx);
        g_host.key_ascii = 0;
        g_host.dirty = 1;
        break;
    case NXW_EV_POINTER:
        vh_pointer(ev);
        break;
    case NXW_EV_FRAME:
        g_host.busy = 0;
        break;
    case NXW_EV_ERROR:
        if (ev->error.op == NXW_OP_SHELL_OPEN) vh_log_code("the shell would not open it, code", ev->error.code);
        else vh_log_code("the window server refused a request, code", ev->error.code);
        break;
    case NXW_EV_OPEN_PATH:
        if (g_host.app->open_path && g_host.app->open_path(ev->open_path.path) != 0)
            vh_log("could not show", ev->open_path.path);
        g_host.dirty = 1;
        break;
    case NXW_EV_DISCONNECTED:
        vh_log("the window server went away", 0);
        g_host.win = 0;
        g_host.running = 0;
        break;
    default:
        break;
    }
}

/* ============ Run ============ */

int viewhost_run(const viewhost_app_t *app, int argc, char **argv) {
    if (!app || !app->title || !app->render || app->width == 0 || app->height == 0) {
        nx_debug_print("[VIEWHOST] invalid app description\n");
        return 2;
    }
    g_host.app = app;

    nxw_display_info_t info;
    int r = nxw_connect(&info);
    if (r != 0) { vh_log_code("cannot reach the window server, code", r); return 1; }

    /* The backbuffer covers the largest window the screen allows. */
    g_host.max_w = info.screen_w && info.screen_w < info.max_size ? info.screen_w : info.max_size;
    g_host.max_h = info.screen_h && info.screen_h < info.max_size ? info.screen_h : info.max_size;
    if (g_host.max_w == 0 || g_host.max_h == 0) { vh_log("the server reported no screen", 0); nxw_disconnect(); return 1; }
    if (nxui_init_app(g_host.max_w, g_host.max_h) != NXC_OK) {
        vh_log("toolkit init failed", 0);
        nxw_disconnect();
        return 1;
    }

    nxw_window_desc_t d;
    d.title = app->title;
    d.width = app->width < g_host.max_w ? app->width : g_host.max_w;
    d.height = app->height < g_host.max_h ? app->height : g_host.max_h;
    d.x = NXW_POS_AUTO;
    d.y = NXW_POS_AUTO;
    d.flags = app->resizable ? NXW_WIN_RESIZABLE : 0;
    r = nxw_window_create(&d, &g_host.win);
    if (r != 0) { vh_log_code("the window server refused the window, code", r); nxw_disconnect(); return 1; }

    g_host.dw.id = 1;
    vh_copy(g_host.dw.title, app->title, (int)sizeof(g_host.dw.title));
    g_host.dw.x = 0;
    g_host.dw.y = -APP_VIEW_TITLE_H;
    g_host.dw.width = d.width;
    g_host.dw.height = d.height;
    nxw_window_get_size(g_host.win, &g_host.dw.width, &g_host.dw.height, 0);
    g_host.dw.visible = 1;
    g_host.dw.focused = 1;
    g_host.dw.no_resize = app->resizable ? 0 : 1;
    g_host.dw.render_content = app->render;
    g_host.dw.key_handler = app->key;
    g_host.dw.mouse_handler = app->mouse;
    g_host.dw.scroll_handler = app->scroll;

    if (app->open && app->open(g_host.dw.id) != 0) {
        vh_log("the view refused to open", 0);
        nxw_disconnect();
        return 1;
    }
    if (argc > 1 && argv && argv[1] && app->open_path) {
        if (app->open_path(argv[1]) != 0) vh_log("could not show", argv[1]);
    }
    vh_log("window open", 0);

    g_host.running = 1;
    g_host.dirty = 1;
    while (g_host.running) {
        uint64_t now = nx_gettime();
        if (g_host.redraw_at && now >= g_host.redraw_at) { g_host.redraw_at = 0; g_host.dirty = 1; }
        if (now - g_host.last_draw_ms >= VH_IDLE_REDRAW_MS) g_host.dirty = 1;
        if (g_host.dirty && !g_host.busy && now - g_host.last_draw_ms >= VH_MIN_FRAME_MS) vh_draw();
        if (!g_host.running) break;

        /* Sleep until an event, the next requested redraw or the idle tick */
        uint64_t wait = VH_IDLE_REDRAW_MS;
        now = nx_gettime();
        if (g_host.dirty && !g_host.busy) wait = VH_MIN_FRAME_MS;
        if (g_host.redraw_at) {
            uint64_t until = g_host.redraw_at > now ? g_host.redraw_at - now : 0;
            if (until < wait) wait = until;
        }
        nxw_event_t ev;
        r = nxw_wait_event(&ev, wait);
        if (r < 0) { vh_log_code("event wait failed:", r); break; }
        if (r == 0) continue;
        vh_event(&ev);
        /* Drain what else is queued before drawing */
        while (g_host.running && nxw_poll_event(&ev) == 1) vh_event(&ev);
    }

    if (g_host.win) nxw_window_destroy(g_host.win);
    g_host.win = 0;
    nxw_disconnect();
    vh_log("exiting", 0);
    return 0;
}
