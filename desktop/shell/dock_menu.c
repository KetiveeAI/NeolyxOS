/*
 * NeolyxOS - Dock context menu (right-click on a dock or taskbar item)
 *
 * A glass popover with the item's windows and actions:
 *   app:     <windows...> | Open, Show All Windows, Hide | Options > | Quit
 *            Options: Keep in Dock (checked when pinned), Remove from Dock
 *   window:  Restore | Close            (minimized-window items)
 *   Trash:   Open
 *
 * The menu remembers its item by key, not index: the model is rebuilt
 * when windows open or close, so actions look the item up again and do
 * nothing if it has gone away.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "dock_internal.h"

typedef enum {
    MA_NONE = 0, MA_WINDOW, MA_OPEN, MA_SHOW_ALL, MA_HIDE, MA_QUIT,
    MA_OPTIONS, MA_KEEP, MA_REMOVE, MA_RESTORE, MA_CLOSE
} menu_action_t;

#define ROW_H        26.0f
#define SEP_H        9.0f
#define PAD          5.0f
#define MAX_ROWS     16
#define MAX_WIN_ROWS 6
#define OPEN_POS_UNSET (-100000)

typedef struct {
    menu_action_t action;       /* MA_NONE = separator */
    int           enabled;
    int           checked;
    uint32_t      win;
    char          text[48];
} menu_row_t;

typedef struct {
    int        open;
    uint32_t   key;              /* item identity */
    menu_row_t rows[MAX_ROWS];
    int        nrows;
    menu_row_t sub[2];
    int        nsub;
    nxc_rect_t box, subbox;
    int        sub_open;
    int        hover, sub_hover;
    float      anim, sub_anim;
    int        open_x, open_y;   /* pointer when opened */
    int        moved;
} dock_menu_t;

static dock_menu_t g_menu;

/* ============ Building ============ */

static void add_row(menu_action_t a, const char *text, int enabled, int checked, uint32_t win) {
    if (g_menu.nrows >= MAX_ROWS) return;
    menu_row_t *r = &g_menu.rows[g_menu.nrows++];
    r->action = a;
    r->enabled = enabled;
    r->checked = checked;
    r->win = win;
    dock_strlcpy(r->text, text ? text : "", (int)sizeof(r->text));
}

static void add_sep(void) {
    if (g_menu.nrows > 0 && g_menu.rows[g_menu.nrows - 1].action != MA_NONE) add_row(MA_NONE, "", 0, 0, 0);
}

static float rows_height(const menu_row_t *rows, int n) {
    float h = 2.0f * PAD;
    for (int i = 0; i < n; i++) h += rows[i].action == MA_NONE ? SEP_H : ROW_H;
    return h;
}

static float rows_width(const menu_row_t *rows, int n) {
    float w = 0.0f;
    for (int i = 0; i < n; i++) {
        if (rows[i].action == MA_NONE) continue;
        float tw = nxui_text_width(NXUI_TEXT_BODY, rows[i].text);
        if (tw > w) w = tw;
    }
    w += 2.0f * PAD + 30.0f + 30.0f;     /* check column + chevron column */
    if (w < 196.0f) w = 196.0f;
    if (w > 300.0f) w = 300.0f;
    return w;
}

static int find_item(uint32_t key) {
    for (int i = 0; i < g_dock_model.nitems; i++)
        if (g_dock_model.items[i].key == key) return i;
    return -1;
}

void dock_menu_open(int item, float ax, float ay, dock_menu_side_t side) {
    if (item < 0 || item >= g_dock_model.nitems) return;
    const dock_item_t *it = &g_dock_model.items[item];
    if (it->type == DOCK_IT_SEP) return;
    g_menu.nrows = 0;
    g_menu.nsub = 0;
    g_menu.key = it->key;

    if (it->type == DOCK_IT_MINI) {
        add_row(MA_RESTORE, "Restore", 1, 0, it->win_id);
        add_sep();
        add_row(MA_CLOSE, "Close", 1, 0, it->win_id);
    } else {
        /* The app's windows first; the focused one is checked. */
        int shown = 0;
        for (int i = 0; i < g_dock_model.nwins && shown < MAX_WIN_ROWS; i++) {
            const dock_win_t *w = &g_dock_model.wins[i];
            int mine = it->type == DOCK_IT_TRASH ? w->kind == NXAPP_TRASH : w->item == item;
            if (!mine) continue;
            add_row(MA_WINDOW, w->title[0] ? w->title : "Untitled", 1, w->focused, w->id);
            shown++;
        }
        if (shown) add_sep();
        add_row(MA_OPEN, "Open", 1, 0, 0);
        if (it->type == DOCK_IT_APP) {
            add_row(MA_SHOW_ALL, "Show All Windows", it->nwin > 0, 0, 0);
            add_row(MA_HIDE, "Hide", it->nwin > it->nmin && dock_hide_available(), 0, 0);
            add_sep();
            add_row(MA_OPTIONS, "Options", 1, 0, 0);
            add_sep();
            add_row(MA_QUIT, "Quit", it->nwin > 0, 0, 0);
            menu_row_t *s = &g_menu.sub[0];
            s->action = MA_KEEP; s->enabled = 1; s->checked = it->pin >= 0; s->win = 0;
            dock_strlcpy(s->text, "Keep in Dock", (int)sizeof(s->text));
            s = &g_menu.sub[1];
            s->action = MA_REMOVE; s->enabled = it->pin >= 0; s->checked = 0; s->win = 0;
            dock_strlcpy(s->text, "Remove from Dock", (int)sizeof(s->text));
            g_menu.nsub = 2;
        } else if (it->nwin > 0) {
            add_sep();
            add_row(MA_QUIT, "Close All", 1, 0, 0);
        }
    }

    float W = (float)nxui_width(), H = (float)nxui_height();
    float mw = rows_width(g_menu.rows, g_menu.nrows);
    float mh = rows_height(g_menu.rows, g_menu.nrows);
    float x, y;
    switch (side) {
        case DOCK_MENU_RIGHT: x = ax;            y = ay - mh * 0.5f; break;
        case DOCK_MENU_LEFT:  x = ax - mw;       y = ay - mh * 0.5f; break;
        default:              x = ax - mw * 0.5f; y = ay - mh;       break;
    }
    float top = nxui_theme()->menubar_height + 6.0f;
    if (x < 6.0f) x = 6.0f;
    if (x + mw > W - 6.0f) x = W - 6.0f - mw;
    if (y < top) y = top;
    if (y + mh > H - 6.0f) y = H - 6.0f - mh;
    g_menu.box = nxc_rect((float)(int)x, (float)(int)y, mw, mh);
    g_menu.open = 1;
    g_menu.sub_open = 0;
    g_menu.hover = -1;
    g_menu.sub_hover = -1;
    g_menu.anim = 0.0f;
    g_menu.sub_anim = 0.0f;
    g_menu.moved = 0;
    g_menu.open_x = OPEN_POS_UNSET;
    g_menu.open_y = OPEN_POS_UNSET;
    dock_log2("menu for ", it->label);
}

int dock_menu_is_open(void) { return g_menu.open; }
int dock_menu_item(void) { return g_menu.open ? find_item(g_menu.key) : -1; }

void dock_menu_close(void) {
    g_menu.open = 0;
    g_menu.sub_open = 0;
    g_menu.hover = -1;
    g_menu.sub_hover = -1;
}

/* ============ Geometry ============ */

static nxc_rect_t row_rect(nxc_rect_t box, const menu_row_t *rows, int idx) {
    float y = box.y + PAD;
    for (int i = 0; i < idx; i++) y += rows[i].action == MA_NONE ? SEP_H : ROW_H;
    return nxc_rect(box.x + PAD, y, box.w - 2.0f * PAD, rows[idx].action == MA_NONE ? SEP_H : ROW_H);
}

static int row_at(nxc_rect_t box, const menu_row_t *rows, int n, int x, int y) {
    if (!nxglass_hit(box, x, y)) return -1;
    for (int i = 0; i < n; i++) {
        if (rows[i].action == MA_NONE) continue;
        if (nxglass_hit(row_rect(box, rows, i), x, y)) return i;
    }
    return -1;
}

static int options_row(void) {
    for (int i = 0; i < g_menu.nrows; i++) if (g_menu.rows[i].action == MA_OPTIONS) return i;
    return -1;
}

static void place_sub(void) {
    int oi = options_row();
    if (oi < 0 || g_menu.nsub == 0) return;
    nxc_rect_t r = row_rect(g_menu.box, g_menu.rows, oi);
    float sw = rows_width(g_menu.sub, g_menu.nsub);
    float sh = rows_height(g_menu.sub, g_menu.nsub);
    float W = (float)nxui_width(), H = (float)nxui_height();
    float x = g_menu.box.x + g_menu.box.w + 3.0f;
    if (x + sw > W - 6.0f) x = g_menu.box.x - 3.0f - sw;
    float y = r.y - PAD;
    if (y + sh > H - 6.0f) y = H - 6.0f - sh;
    g_menu.subbox = nxc_rect((float)(int)x, (float)(int)y, sw, sh);
}

/* ============ Rendering ============ */

static void draw_rows(nxc_canvas_t *c, nxc_rect_t box, const menu_row_t *rows, int n, int hover, int sub_open) {
    const nxui_theme_t *th = nxui_theme();
    for (int i = 0; i < n; i++) {
        nxc_rect_t r = row_rect(box, rows, i);
        const menu_row_t *row = &rows[i];
        if (row->action == MA_NONE) {
            nxglass_separator(c, box.x + 12.0f, r.y + SEP_H * 0.5f, box.w - 24.0f);
            continue;
        }
        int hot = row->enabled && (i == hover || (row->action == MA_OPTIONS && sub_open));
        if (hot) nxc_fill_rrect(c, r, 6.0f, th->accent);
        nxc_color_t fg = hot ? th->text_on_accent : (row->enabled ? th->text_primary : th->text_tertiary);
        if (row->checked)
            nxsym_draw(c, NXSYM_CHECK, r.x + 13.0f, r.y + r.h * 0.5f, 12.0f, fg, 0.0f);
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(r.x + 26.0f, r.y, r.w - 52.0f, r.h), row->text, fg,
                          NXC_ALIGN_LEFT);
        if (row->action == MA_OPTIONS)
            nxsym_draw(c, NXSYM_CHEVRON_RIGHT, r.x + r.w - 13.0f, r.y + r.h * 0.5f, 11.0f, fg, 0.0f);
    }
}

void dock_menu_render(nxc_canvas_t *c, int mouse_x, int mouse_y) {
    (void)mouse_x; (void)mouse_y;
    if (!c || !g_menu.open) return;
    const nxui_theme_t *th = nxui_theme();
    float dt = nxui_frame_dt();
    g_menu.anim = nxui_approach(g_menu.anim, 1.0f, 0.035f, dt);
    float e = nxui_ease_out_cubic(g_menu.anim);

    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(e * 255.0f));
    nxglass_panel(c, g_menu.box, 10.0f, NXGLASS_REGULAR, 2, 0);
    nxc_stroke_rrect(c, g_menu.box, 10.0f, 1.0f, th->window_border);
    draw_rows(c, g_menu.box, g_menu.rows, g_menu.nrows, g_menu.hover, g_menu.sub_open);
    nxc_restore(c);

    if (g_menu.sub_open && g_menu.nsub > 0) {
        g_menu.sub_anim = nxui_approach(g_menu.sub_anim, 1.0f, 0.03f, dt);
        nxc_save(c);
        nxc_set_alpha(c, (uint8_t)(nxui_ease_out_cubic(g_menu.sub_anim) * 255.0f));
        nxglass_panel(c, g_menu.subbox, 10.0f, NXGLASS_REGULAR, 2, 0);
        nxc_stroke_rrect(c, g_menu.subbox, 10.0f, 1.0f, th->window_border);
        draw_rows(c, g_menu.subbox, g_menu.sub, g_menu.nsub, g_menu.sub_hover, 0);
        nxc_restore(c);
    } else {
        g_menu.sub_anim = 0.0f;
    }
}

/* ============ Actions ============ */

static void run(const menu_row_t *row) {
    if (!row || !row->enabled) return;
    int item = find_item(g_menu.key);
    menu_action_t a = row->action;
    uint32_t win = row->win;
    if (a == MA_OPTIONS) return;
    dock_menu_close();
    switch (a) {
        case MA_WINDOW:
        case MA_RESTORE:
            if (desktop_activate_window(win) != 0) dock_log("warning: menu window no longer exists");
            break;
        case MA_CLOSE:
            if (desktop_close_window(win) != 0) dock_log("warning: menu window no longer exists");
            break;
        default:
            if (item < 0) { dock_log("warning: menu item no longer in dock"); return; }
            switch (a) {
                case MA_OPEN:     dock_item_primary(item); break;
                case MA_SHOW_ALL: dock_item_show_all(item); break;
                case MA_HIDE:     dock_item_hide(item); break;
                case MA_QUIT:     dock_item_quit(item); break;
                case MA_KEEP:
                    if (g_dock_model.items[item].pin >= 0) dock_item_unpin(item);
                    else dock_item_keep(item);
                    break;
                case MA_REMOVE:   dock_item_unpin(item); break;
                default: break;
            }
            break;
    }
}

/* ============ Input ============ */

int dock_menu_handle_mouse(int x, int y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_menu.open) return 0;
    int press = (buttons & 0x03) && !(prev_buttons & 0x03);
    int rrelease = !(buttons & 0x02) && (prev_buttons & 0x02);
    int lrelease = !(buttons & 0x01) && (prev_buttons & 0x01);
    if (!g_menu.moved) {
        int dx = x - g_menu.open_x, dy = y - g_menu.open_y;
        if (g_menu.open_x == OPEN_POS_UNSET) { g_menu.open_x = x; g_menu.open_y = y; }
        else if (dx * dx + dy * dy > 16) g_menu.moved = 1;
    }

    int in_sub = g_menu.sub_open && nxglass_hit(g_menu.subbox, x, y);
    g_menu.sub_hover = in_sub ? row_at(g_menu.subbox, g_menu.sub, g_menu.nsub, x, y) : -1;
    if (!in_sub) {
        int h = row_at(g_menu.box, g_menu.rows, g_menu.nrows, x, y);
        if (h >= 0 || nxglass_hit(g_menu.box, x, y)) g_menu.hover = h;
        if (h >= 0 && g_menu.rows[h].action == MA_OPTIONS) {
            if (!g_menu.sub_open) { place_sub(); g_menu.sub_open = 1; g_menu.sub_anim = 0.0f; }
        } else if (h >= 0) {
            g_menu.sub_open = 0;
        }
    }

    if (press) {
        if (in_sub) {
            if (g_menu.sub_hover >= 0) run(&g_menu.sub[g_menu.sub_hover]);
        } else if (nxglass_hit(g_menu.box, x, y)) {
            if (g_menu.hover >= 0) {
                const menu_row_t *row = &g_menu.rows[g_menu.hover];
                if (row->action == MA_OPTIONS) {
                    if (!g_menu.sub_open) place_sub();
                    g_menu.sub_open = !g_menu.sub_open;
                } else {
                    run(row);
                }
            }
        } else {
            dock_menu_close();
        }
        return 1;
    }
    /* Press-drag-release from the right click that opened the menu. */
    if ((rrelease || lrelease) && g_menu.moved) {
        if (in_sub && g_menu.sub_hover >= 0) run(&g_menu.sub[g_menu.sub_hover]);
        else if (!in_sub && g_menu.hover >= 0 && g_menu.rows[g_menu.hover].action != MA_OPTIONS &&
                 nxglass_hit(g_menu.box, x, y))
            run(&g_menu.rows[g_menu.hover]);
    }
    return 1;
}
