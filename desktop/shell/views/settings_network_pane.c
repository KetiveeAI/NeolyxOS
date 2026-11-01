/*
 * NeolyxOS - Settings > Network pane (Networking area)
 *
 * Reads everything through the NXNet client (nxnet.h):
 *   - interface list with status dots (Ethernet, Wi-Fi; loopback hidden)
 *   - detail card for the selected interface (IP, mask, router, DNS, MAC)
 *   - Wi-Fi power toggle, network list with signal bars and lock icons
 *   - join sheet with password field (validated 8..63 before submit)
 *   - Advanced sheet: DHCP or manual IPv4 with per-field validation
 *   - an honest "Networking service unavailable" state when the client
 *     returns NXNET_ERR_UNSUPPORTED (current OS kernels)
 *
 * Registered by the Settings app as settings_pane_network (settings_pane.h).
 * Coordinates: render gets the absolute detail area; mouse gets
 * pane-local coordinates.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_pane.h"
#include "network_indicator.h"
#include "nxglass.h"
#include "nxframe.h"
#include "nxicons.h"
#include "nxnet.h"
#include "nxui.h"
#include "nxsyscall.h"

/* ============ Local helpers ============ */

static void np_strcpy(char *d, const char *s, uint32_t cap) {
    if (!d || cap == 0) return;
    uint32_t i = 0;
    if (s) while (i + 1 < cap && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static uint32_t np_strlen(const char *s) {
    uint32_t n = 0;
    if (s) while (s[n]) n++;
    return n;
}

static int np_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void np_wipe(char *p, uint32_t n) {
    volatile char *v = p;
    for (uint32_t i = 0; i < n; i++) v[i] = 0;
}

/* Append s to buf at pos (cap-bounded). Returns new pos. */
static uint32_t np_app(char *buf, uint32_t pos, uint32_t cap, const char *s) {
    if (!buf || cap == 0) return pos;
    while (s && *s && pos + 1 < cap) buf[pos++] = *s++;
    buf[pos < cap ? pos : cap - 1] = '\0';
    return pos;
}

static uint32_t np_app_u(char *buf, uint32_t pos, uint32_t cap, uint64_t v) {
    char t[24];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 23);
    char r[24];
    for (int i = 0; i < n; i++) r[i] = t[n - 1 - i];
    r[n] = '\0';
    return np_app(buf, pos, cap, r);
}

/* "1.7 GB", "200 MB", "512 KB" */
static void np_fmt_bytes(char *buf, uint32_t cap, uint64_t b) {
    static const char *units[] = { "bytes", "KB", "MB", "GB", "TB" };
    uint64_t whole = b, frac = 0;
    int u = 0;
    while (whole >= 1024 && u < 4) { frac = (whole % 1024) * 10 / 1024; whole /= 1024; u++; }
    uint32_t p = np_app_u(buf, 0, cap, whole);
    if (u > 0 && whole < 10) { p = np_app(buf, p, cap, "."); p = np_app_u(buf, p, cap, frac); }
    p = np_app(buf, p, cap, " ");
    np_app(buf, p, cap, units[u]);
}

static void np_fmt_speed(char *buf, uint32_t cap, uint32_t mbps) {
    uint32_t p;
    if (mbps == 0) { np_strcpy(buf, "Unknown", cap); return; }
    if (mbps >= 1000 && mbps % 1000 == 0) { p = np_app_u(buf, 0, cap, mbps / 1000); np_app(buf, p, cap, " Gb/s"); }
    else { p = np_app_u(buf, 0, cap, mbps); np_app(buf, p, cap, " Mb/s"); }
}

static void np_fmt_ip(char *buf, uint32_t cap, nxnet_ipv4_t ip) {
    if (nxnet_ipv4_is_zero(ip) || nxnet_ipv4_format(ip, buf, cap) != NXNET_OK) np_strcpy(buf, "--", cap);
}

/* ============ State ============ */

#define NP_MAX_IFS   NXNET_MAX_INTERFACES
#define NP_MAX_NETS  12
#define NP_FIELD_CAP 16

typedef enum { NP_SHEET_NONE = 0, NP_SHEET_JOIN, NP_SHEET_ADVANCED } np_sheet_t;

/* Advanced sheet text fields */
enum { NP_F_ADDR = 0, NP_F_MASK, NP_F_ROUTER, NP_F_DNS, NP_F_COUNT };

/* Hover / hit ids */
enum {
    NP_HIT_NONE = 0,
    NP_HIT_IF_BASE = 100,          /* + interface slot */
    NP_HIT_NET_BASE = 200,         /* + network slot */
    NP_HIT_WIFI_TOGGLE = 300,
    NP_HIT_ADVANCED,
    NP_HIT_FORGET,
    NP_HIT_SCAN,
    NP_HIT_RETRY,
    NP_HIT_SHEET_CANCEL,
    NP_HIT_SHEET_OK,
    NP_HIT_SHEET_FIELD,            /* + field index */
    NP_HIT_SHOW_PASS = 320,
    NP_HIT_SEGMENT
};

static struct {
    int                  ready;
    nxnet_status_t       avail;
    uint32_t             gen_seen;
    float                last_refresh;

    nxnet_interface_t    ifs[NP_MAX_IFS];   /* loopback filtered out */
    uint32_t             nif;
    int                  sel;               /* slot in ifs */
    uint32_t             sel_index;         /* nxnet index, to keep selection across refresh */

    nxnet_status_t       wifi_st;
    nxnet_wifi_status_t  wifi;
    nxnet_wifi_network_t nets[NP_MAX_NETS];
    uint32_t             nnets;
    char                 hostname[NXNET_HOSTNAME_MAX + 1];

    int                  hover;
    float                toggle_t;          /* animated Wi-Fi toggle 0..1 */

    char                 banner[96];        /* last action result */
    int                  banner_err;
    float                banner_until;

    np_sheet_t           sheet;
    /* join */
    char                 join_ssid[NXNET_SSID_MAX + 1];
    nxnet_wifi_security_t join_sec;
    char                 pass[NXNET_PASSPHRASE_MAX + 2];
    int                  show_pass;
    char                 sheet_err[96];
    /* advanced */
    uint32_t             adv_if_index;
    int                  adv_method;        /* 0 DHCP, 1 manual */
    char                 fields[NP_F_COUNT][NP_FIELD_CAP];
    int                  focus;             /* field index, -1 none */
    nxnet_field_t        bad_field;

    uint8_t              shift;
    nxc_rect_t           area;
} g_np;

static void np_set_banner(const char *msg, int err) {
    np_strcpy(g_np.banner, msg, sizeof(g_np.banner));
    g_np.banner_err = err;
    g_np.banner_until = nxui_time() + 4.0f;
}

static const nxnet_interface_t *np_selected(void) {
    if (g_np.sel < 0 || (uint32_t)g_np.sel >= g_np.nif) return 0;
    return &g_np.ifs[g_np.sel];
}

/* ============ Model refresh ============ */

static void np_refresh(void) {
    g_np.ready = 1;
    g_np.last_refresh = nxui_time();
    g_np.avail = nxnet_available();
    g_np.nif = 0;
    g_np.nnets = 0;
    g_np.wifi_st = NXNET_ERR_UNSUPPORTED;
    g_np.hostname[0] = '\0';
    if (g_np.avail != NXNET_OK) {
        g_np.gen_seen = nxnet_generation();
        return;
    }

    nxnet_interface_t all[NXNET_MAX_INTERFACES];
    uint32_t n = 0;
    nxnet_status_t st = nxnet_interfaces(all, NXNET_MAX_INTERFACES, &n);
    if (st == NXNET_ERR_BUFFER_TOO_SMALL) { n = NXNET_MAX_INTERFACES; st = NXNET_OK; }
    if (st != NXNET_OK) {
        g_np.avail = st;
        g_np.gen_seen = nxnet_generation();
        return;
    }
    int keep = -1;
    for (uint32_t i = 0; i < n && g_np.nif < NP_MAX_IFS; i++) {
        if (all[i].kind == NXNET_IF_LOOPBACK) continue;
        if (all[i].index == g_np.sel_index) keep = (int)g_np.nif;
        g_np.ifs[g_np.nif++] = all[i];
    }
    if (keep < 0) {
        keep = 0;
        for (uint32_t i = 0; i < g_np.nif; i++) if (g_np.ifs[i].primary) { keep = (int)i; break; }
    }
    g_np.sel = g_np.nif ? keep : -1;
    if (g_np.sel >= 0) g_np.sel_index = g_np.ifs[g_np.sel].index;

    g_np.wifi_st = nxnet_wifi_status(&g_np.wifi);
    if (g_np.wifi_st == NXNET_OK && g_np.wifi.state != NXNET_WIFI_OFF) {
        uint32_t cnt = 0;
        st = nxnet_wifi_scan_results(g_np.nets, NP_MAX_NETS, &cnt);
        if (st == NXNET_OK || st == NXNET_ERR_BUFFER_TOO_SMALL)
            g_np.nnets = cnt < NP_MAX_NETS ? cnt : NP_MAX_NETS;
    }
    if (nxnet_hostname_get(g_np.hostname, sizeof(g_np.hostname)) != NXNET_OK) g_np.hostname[0] = '\0';
    g_np.gen_seen = nxnet_generation();
}

static void np_maybe_refresh(void) {
    if (!g_np.ready || g_np.gen_seen != nxnet_generation() || nxui_time() - g_np.last_refresh > 3.0f)
        np_refresh();
}

/* ============ Layout ============ */

#define NP_PAD      24.0f
#define NP_IF_ROW   50.0f
#define NP_NET_ROW  32.0f
#define NP_DET_ROW  40.0f

typedef struct {
    nxc_rect_t title;
    nxc_rect_t left, right;
    nxc_rect_t if_card;
    nxc_rect_t header;
    nxc_rect_t toggle;      /* Wi-Fi power */
    nxc_rect_t details;
    nxc_rect_t list_label;
    nxc_rect_t scan_btn;
    nxc_rect_t list;
    nxc_rect_t adv_btn;
    nxc_rect_t forget_btn;
    nxc_rect_t footer;
} np_layout_t;

static np_layout_t np_layout(nxc_rect_t a) {
    np_layout_t L;
    float lw = a.w < 620.0f ? 170.0f : 200.0f;
    L.title = nxc_rect(a.x + NP_PAD, a.y + 18.0f, a.w - 2.0f * NP_PAD, 34.0f);
    float top = a.y + 66.0f, bottom = a.y + a.h - NP_PAD;
    L.left = nxc_rect(a.x + NP_PAD, top, lw, bottom - top);
    L.right = nxc_rect(L.left.x + lw + 16.0f, top, a.x + a.w - NP_PAD - (L.left.x + lw + 16.0f), bottom - top);
    float rows = (float)(g_np.nif ? g_np.nif : 1);
    L.if_card = nxc_rect(L.left.x, top, lw, rows * NP_IF_ROW + 8.0f);

    L.header = nxc_rect(L.right.x, top, L.right.w, 64.0f);
    L.toggle = nxc_rect(L.header.x + L.header.w - 16.0f - NXGLASS_TOGGLE_W,
                        L.header.y + (L.header.h - NXGLASS_TOGGLE_H) * 0.5f, NXGLASS_TOGGLE_W, NXGLASS_TOGGLE_H);
    L.details = nxc_rect(L.right.x, L.header.y + L.header.h + 12.0f, L.right.w, 3.0f * NP_DET_ROW + 12.0f);
    L.list_label = nxc_rect(L.right.x, L.details.y + L.details.h + 14.0f, L.right.w, 20.0f);
    L.scan_btn = nxc_rect(L.right.x + L.right.w - 64.0f, L.list_label.y - 4.0f, 64.0f, 24.0f);
    float btn_y = bottom - NXGLASS_BUTTON_H;
    L.list = nxc_rect(L.right.x, L.list_label.y + 24.0f, L.right.w, btn_y - 12.0f - (L.list_label.y + 24.0f));
    L.adv_btn = nxc_rect(L.right.x + L.right.w - 104.0f, btn_y, 104.0f, NXGLASS_BUTTON_H);
    L.forget_btn = nxc_rect(L.right.x, btn_y, 132.0f, NXGLASS_BUTTON_H);
    L.footer = nxc_rect(L.left.x, bottom - 40.0f, lw, 40.0f);
    return L;
}

static uint32_t np_visible_nets(const np_layout_t *L) {
    int fit = (int)((L->list.h - 8.0f) / NP_NET_ROW);
    if (fit < 0) fit = 0;
    return g_np.nnets < (uint32_t)fit ? g_np.nnets : (uint32_t)fit;
}

static nxc_rect_t np_net_row(const np_layout_t *L, uint32_t i) {
    return nxc_rect(L->list.x + 4.0f, L->list.y + 4.0f + (float)i * NP_NET_ROW, L->list.w - 8.0f, NP_NET_ROW);
}

static nxc_rect_t np_if_row(const np_layout_t *L, uint32_t i) {
    return nxc_rect(L->if_card.x + 4.0f, L->if_card.y + 4.0f + (float)i * NP_IF_ROW, L->if_card.w - 8.0f, NP_IF_ROW);
}

/* Sheets */
static nxc_rect_t np_sheet_rect(nxc_rect_t a) {
    float w = g_np.sheet == NP_SHEET_JOIN ? 380.0f : 440.0f;
    float h = g_np.sheet == NP_SHEET_JOIN ? 236.0f : 344.0f;
    return nxc_rect(a.x + (a.w - w) * 0.5f, a.y + 64.0f, w, h);
}

static nxc_rect_t np_sheet_ok(nxc_rect_t s) {
    return nxc_rect(s.x + s.w - 20.0f - 88.0f, s.y + s.h - 20.0f - NXGLASS_BUTTON_H, 88.0f, NXGLASS_BUTTON_H);
}

static nxc_rect_t np_sheet_cancel(nxc_rect_t s) {
    nxc_rect_t ok = np_sheet_ok(s);
    return nxc_rect(ok.x - 10.0f - 88.0f, ok.y, 88.0f, NXGLASS_BUTTON_H);
}

static nxc_rect_t np_join_field(nxc_rect_t s) { return nxc_rect(s.x + 20.0f, s.y + 104.0f, s.w - 40.0f, NXGLASS_FIELD_H); }
static nxc_rect_t np_join_show(nxc_rect_t s)  { return nxc_rect(s.x + 20.0f, s.y + 144.0f, 140.0f, 20.0f); }
static nxc_rect_t np_adv_segment(nxc_rect_t s) { return nxc_rect(s.x + 132.0f, s.y + 56.0f, s.w - 152.0f, 28.0f); }
static nxc_rect_t np_adv_field(nxc_rect_t s, int i) {
    return nxc_rect(s.x + 132.0f, s.y + 100.0f + (float)i * 42.0f, s.w - 152.0f, NXGLASS_FIELD_H);
}

/* ============ Drawing pieces ============ */

static nxc_color_t np_state_color(nxnet_if_state_t st) {
    const nxui_theme_t *th = nxui_theme();
    switch (st) {
        case NXNET_IF_CONNECTED:   return th->success;
        case NXNET_IF_CONFIGURING: return th->warning;
        case NXNET_IF_ERROR:       return th->destructive;
        default:                   return th->text_tertiary;
    }
}

static nxsym_t np_if_symbol(const nxnet_interface_t *f) {
    if (f->kind == NXNET_IF_WIFI) return f->state == NXNET_IF_DISABLED ? NXSYM_WIFI_OFF : NXSYM_WIFI;
    return NXSYM_ETHERNET;
}

static const char *np_if_status_line(const nxnet_interface_t *f, char *buf, uint32_t cap) {
    if (f->kind == NXNET_IF_WIFI && f->state == NXNET_IF_CONNECTED && g_np.wifi_st == NXNET_OK &&
        g_np.wifi.state == NXNET_WIFI_CONNECTED) {
        np_strcpy(buf, g_np.wifi.ssid, cap);
        return buf;
    }
    return nxnet_if_state_str(f->state);
}

/* Small rounded tile behind a symbol, tinted by state. on_selection: the
 * tile sits on an accent selection row, so it uses a light frosted fill. */
static void np_symbol_tile_ex(nxc_canvas_t *c, float x, float y, float size, nxsym_t sym, int active,
                              int on_selection) {
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t r = nxc_rect(x, y, size, size);
    nxc_color_t fill = on_selection ? NXC_ARGB(70, 0xFF, 0xFF, 0xFF) : (active ? th->accent : th->control_fill_hover);
    nxc_color_t ink = (active || on_selection) ? th->text_on_accent : th->text_secondary;
    nxc_fill_rrect(c, r, size * 0.28f, fill);
    nxsym_draw(c, sym, x + size * 0.5f, y + size * 0.5f, size * 0.58f, ink, 1.0f);
}

static void np_symbol_tile(nxc_canvas_t *c, float x, float y, float size, nxsym_t sym, int active) {
    np_symbol_tile_ex(c, x, y, size, sym, active, 0);
}

static void np_draw_title(nxc_canvas_t *c, const np_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    nxui_text(c, NXUI_TEXT_TITLE1, L->title.x, L->title.y + 24.0f, "Network", th->text_primary, 0);
    nxc_fill_rrect(c, nxc_rect(L->title.x, L->title.y + 34.0f, 22.0f, 3.0f), 1.5f, th->accent);   /* Neolyx tick */
}

static void np_draw_interfaces(nxc_canvas_t *c, const np_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    nxframe_panel(c, L->if_card);
    for (uint32_t i = 0; i < g_np.nif; i++) {
        const nxnet_interface_t *f = &g_np.ifs[i];
        nxc_rect_t r = np_if_row(L, i);
        int sel = (int)i == g_np.sel;
        if (sel) nxframe_selection(c, r);
        else if (g_np.hover == NP_HIT_IF_BASE + (int)i) nxc_fill_rrect(c, r, 8.0f, th->control_fill_hover);
        np_symbol_tile_ex(c, r.x + 8.0f, r.y + 9.0f, 32.0f, np_if_symbol(f), f->state == NXNET_IF_CONNECTED, 0);
        float tx = r.x + 50.0f, tw = r.w - 58.0f;
        nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, tw, 0 };
        nxui_text(c, NXUI_TEXT_BODY_MEDIUM, tx, r.y + 21.0f, nxnet_if_kind_str(f->kind),
                  th->text_primary, &o);
        char buf[48];
        nxc_fill_circle(c, tx + 3.5f, r.y + 34.0f, 3.5f, np_state_color(f->state));
        o.max_width = tw - 12.0f;
        nxui_text(c, NXUI_TEXT_CAPTION, tx + 12.0f, r.y + 38.0f, np_if_status_line(f, buf, sizeof(buf)),
                  th->text_secondary, &o);
    }
    if (g_np.nif == 0) {
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, L->if_card, "No network hardware", th->text_secondary, NXC_ALIGN_CENTER);
    }
    if (g_np.hostname[0]) {
        nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, L->footer.w, 0 };
        nxui_text(c, NXUI_TEXT_CAPTION, L->footer.x + 4.0f, L->footer.y + 14.0f, "Computer name", th->text_tertiary, &o);
        nxui_text(c, NXUI_TEXT_FOOTNOTE, L->footer.x + 4.0f, L->footer.y + 32.0f, g_np.hostname, th->text_secondary, &o);
    }
}

static void np_detail_cell(nxc_canvas_t *c, float x, float y, float w, const char *label, const char *value, int mono) {
    const nxui_theme_t *th = nxui_theme();
    nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, w - 8.0f, 0 };
    nxui_text(c, NXUI_TEXT_CAPTION, x, y + 14.0f, label, th->text_secondary, &o);
    nxui_text(c, mono ? NXUI_TEXT_MONO : NXUI_TEXT_BODY, x, y + 32.0f, value, th->text_primary, &o);
}

static void np_draw_header(nxc_canvas_t *c, const np_layout_t *L, const nxnet_interface_t *f) {
    const nxui_theme_t *th = nxui_theme();
    nxframe_panel(c, L->header);
    np_symbol_tile(c, L->header.x + 14.0f, L->header.y + 12.0f, 40.0f, np_if_symbol(f), f->state == NXNET_IF_CONNECTED);
    float tx = L->header.x + 66.0f;
    nxui_text(c, NXUI_TEXT_TITLE3, tx, L->header.y + 28.0f, nxnet_if_kind_str(f->kind), th->text_primary, 0);
    char line[96];
    uint32_t p = 0;
    if (f->kind == NXNET_IF_WIFI && g_np.wifi_st == NXNET_OK && g_np.wifi.state == NXNET_WIFI_CONNECTED) {
        p = np_app(line, p, sizeof(line), "Connected to ");
        p = np_app(line, p, sizeof(line), g_np.wifi.ssid);
    } else {
        p = np_app(line, p, sizeof(line), nxnet_if_state_str(f->state));
        if (f->kind == NXNET_IF_ETHERNET && f->link_up && f->speed_mbps) {
            char sp[24];
            np_fmt_speed(sp, sizeof(sp), f->speed_mbps);
            p = np_app(line, p, sizeof(line), "  \xC2\xB7  ");
            p = np_app(line, p, sizeof(line), sp);
        }
    }
    (void)p;
    nxc_fill_circle(c, tx + 3.5f, L->header.y + 42.5f, 3.5f, np_state_color(f->state));
    nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, L->toggle.x - tx - 24.0f, 0 };
    nxui_text(c, NXUI_TEXT_FOOTNOTE, tx + 12.0f, L->header.y + 47.0f, line, th->text_secondary, &o);

    if (f->kind == NXNET_IF_WIFI) {
        float target = f->state == NXNET_IF_DISABLED ? 0.0f : 1.0f;
        g_np.toggle_t = nxui_approach(g_np.toggle_t, target, 0.06f, nxui_frame_dt());
        uint32_t st = g_np.hover == NP_HIT_WIFI_TOGGLE ? NXGLASS_HOVER : 0;
        nxglass_toggle(c, L->toggle.x, L->toggle.y, g_np.toggle_t, st);
    } else if (f->primary) {
        float bw = nxui_text_width(NXUI_TEXT_CAPTION, "Primary") + 14.0f;
        nxglass_badge(c, L->header.x + L->header.w - 16.0f - bw, L->header.y + 23.0f, "Primary", th->accent);
    }
}

static void np_draw_details(nxc_canvas_t *c, const np_layout_t *L, const nxnet_interface_t *f) {
    nxframe_panel(c, L->details);
    float cw = (L->details.w - 32.0f) * 0.5f;
    float x0 = L->details.x + 16.0f, x1 = x0 + cw + 8.0f, y = L->details.y + 6.0f;
    char ip[NXNET_IPV4_STR_MAX], mask[NXNET_IPV4_STR_MAX], gw[NXNET_IPV4_STR_MAX];
    char dns[2 * NXNET_IPV4_STR_MAX + 4], mac[NXNET_MAC_STR_MAX];
    np_fmt_ip(ip, sizeof(ip), f->ipv4.address);
    np_fmt_ip(mask, sizeof(mask), f->ipv4.netmask);
    np_fmt_ip(gw, sizeof(gw), f->ipv4.gateway);
    dns[0] = '\0';
    uint32_t p = 0;
    for (uint32_t i = 0; i < f->ipv4.dns_count && i < 2; i++) {
        char one[NXNET_IPV4_STR_MAX];
        if (nxnet_ipv4_format(f->ipv4.dns[i], one, sizeof(one)) != NXNET_OK) continue;
        if (p) p = np_app(dns, p, sizeof(dns), ", ");
        p = np_app(dns, p, sizeof(dns), one);
    }
    if (!dns[0]) np_strcpy(dns, "--", sizeof(dns));
    if (nxnet_mac_format(f->mac, mac, sizeof(mac)) != NXNET_OK) np_strcpy(mac, "--", sizeof(mac));
    int off = f->state != NXNET_IF_CONNECTED;
    np_detail_cell(c, x0, y, cw, "IP Address", off ? "--" : ip, 0);
    np_detail_cell(c, x1, y, cw, "Subnet Mask", off ? "--" : mask, 0);
    y += NP_DET_ROW;
    np_detail_cell(c, x0, y, cw, "Router", off ? "--" : gw, 0);
    np_detail_cell(c, x1, y, cw, "DNS Servers", off ? "--" : dns, 0);
    y += NP_DET_ROW;
    np_detail_cell(c, x0, y, cw, "MAC Address", mac, 1);
    np_detail_cell(c, x1, y, cw, "Configure IPv4", f->ipv4.method == NXNET_IPV4_DHCP ? "Using DHCP" : "Manually", 0);
}

/* Signal bars drawn locally (four rising bars) so strength reads at a glance. */
static void np_signal_bars(nxc_canvas_t *c, float x, float base_y, uint8_t bars, nxc_color_t on, nxc_color_t off) {
    for (int i = 0; i < 4; i++) {
        float h = 4.0f + 3.0f * (float)i;
        nxc_fill_rrect(c, nxc_rect(x + (float)i * 4.5f, base_y - h, 3.0f, h), 1.0f, i < bars ? on : off);
    }
}

static void np_draw_networks(nxc_canvas_t *c, const np_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    nxframe_caps(c, L->list_label.x + 2.0f, L->list_label.y + 14.0f,
                 g_np.wifi.state == NXNET_WIFI_OFF ? "Wi-Fi" : "Networks", th->text_secondary);
    if (g_np.wifi.state != NXNET_WIFI_OFF)
        nxglass_button(c, L->scan_btn, "Scan", NXGLASS_BUTTON_PLAIN, g_np.hover == NP_HIT_SCAN ? NXGLASS_HOVER : 0);
    nxframe_panel(c, L->list);
    if (g_np.wifi.state == NXNET_WIFI_OFF) {
        nxui_text_in_rect(c, NXUI_TEXT_BODY, L->list, "Wi-Fi is turned off", th->text_secondary, NXC_ALIGN_CENTER);
        return;
    }
    if (g_np.nnets == 0) {
        nxui_text_in_rect(c, NXUI_TEXT_BODY, L->list, "No networks found", th->text_secondary, NXC_ALIGN_CENTER);
        return;
    }
    uint32_t vis = np_visible_nets(L);
    for (uint32_t i = 0; i < vis; i++) {
        const nxnet_wifi_network_t *n = &g_np.nets[i];
        nxc_rect_t r = np_net_row(L, i);
        if (g_np.hover == NP_HIT_NET_BASE + (int)i) nxc_fill_rrect(c, r, 7.0f, th->control_fill_hover);
        if (i > 0 && g_np.hover != NP_HIT_NET_BASE + (int)i && g_np.hover != NP_HIT_NET_BASE + (int)i - 1)
            nxglass_separator(c, r.x + 34.0f, r.y, r.w - 44.0f);
        float cy = r.y + r.h * 0.5f;
        if (n->current) nxsym_draw(c, NXSYM_CHECK, r.x + 16.0f, cy, 13.0f, th->accent, 0.0f);
        float rx = r.x + r.w - 12.0f;
        np_signal_bars(c, rx - 15.0f, cy + 6.0f, nxnet_rssi_bars(n->rssi_dbm), th->text_primary,
                       NXC_WITH_ALPHA(th->text_tertiary, 90));
        rx -= 26.0f;
        if (n->security != NXNET_WIFI_SEC_OPEN) {
            nxsym_draw(c, NXSYM_LOCK, rx - 6.0f, cy, 12.0f, th->text_secondary, 0.0f);
            rx -= 20.0f;
        }
        const char *tag = n->current ? 0 : (n->known ? "Known" : 0);
        if (tag) {
            float tw = nxui_text_width(NXUI_TEXT_CAPTION, tag);
            nxui_text(c, NXUI_TEXT_CAPTION, rx - tw, cy + 4.0f, tag, th->text_tertiary, 0);
            rx -= tw + 10.0f;
        }
        nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, rx - (r.x + 34.0f) - 6.0f, 0 };
        nxui_text(c, n->current ? NXUI_TEXT_BODY_MEDIUM : NXUI_TEXT_BODY, r.x + 34.0f, cy + 4.5f, n->ssid,
                  th->text_primary, &o);
    }
}

static void np_draw_ethernet_traffic(nxc_canvas_t *c, const np_layout_t *L, const nxnet_interface_t *f) {
    const nxui_theme_t *th = nxui_theme();
    nxframe_caps(c, L->list_label.x + 2.0f, L->list_label.y + 14.0f, "Traffic", th->text_secondary);
    nxc_rect_t card = nxc_rect(L->list.x, L->list.y, L->list.w, 2.0f * NP_DET_ROW + 12.0f);
    nxframe_panel(c, card);
    float cw = (card.w - 32.0f) * 0.5f;
    char a[32], b[32];
    np_fmt_bytes(a, sizeof(a), f->counters.rx_bytes);
    np_fmt_bytes(b, sizeof(b), f->counters.tx_bytes);
    np_detail_cell(c, card.x + 16.0f, card.y + 6.0f, cw, "Received", a, 0);
    np_detail_cell(c, card.x + 24.0f + cw, card.y + 6.0f, cw, "Sent", b, 0);
    char pa[24], pb[24];
    np_app_u(pa, 0, sizeof(pa), f->counters.rx_packets);
    np_app_u(pb, 0, sizeof(pb), f->counters.tx_packets);
    np_detail_cell(c, card.x + 16.0f, card.y + 6.0f + NP_DET_ROW, cw, "Packets In", pa, 0);
    np_detail_cell(c, card.x + 24.0f + cw, card.y + 6.0f + NP_DET_ROW, cw, "Packets Out", pb, 0);
}

static void np_draw_unavailable(nxc_canvas_t *c, nxc_rect_t a) {
    const nxui_theme_t *th = nxui_theme();
    np_layout_t L = np_layout(a);
    np_draw_title(c, &L);
    float w = a.w - 2.0f * NP_PAD;
    if (w > 440.0f) w = 440.0f;
    nxc_rect_t card = nxc_rect(a.x + (a.w - w) * 0.5f, a.y + 110.0f, w, 236.0f);
    nxframe_panel(c, card);
    float cx = card.x + card.w * 0.5f;
    nxc_fill_circle(c, cx, card.y + 52.0f, 28.0f, NXC_WITH_ALPHA(th->warning, 40));
    nxsym_draw(c, NXSYM_WIFI_OFF, cx, card.y + 52.0f, 28.0f, th->warning, 0.0f);
    nxui_text_in_rect(c, NXUI_TEXT_TITLE3, nxc_rect(card.x, card.y + 92.0f, card.w, 24.0f),
                      "Networking service unavailable", th->text_primary, NXC_ALIGN_CENTER);
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(card.x + 20.0f, card.y + 122.0f, card.w - 40.0f, 18.0f),
                      "This system does not report network status to apps yet.",
                      th->text_secondary, NXC_ALIGN_CENTER);
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(card.x + 20.0f, card.y + 140.0f, card.w - 40.0f, 18.0f),
                      g_np.avail == NXNET_ERR_UNSUPPORTED ? "Connections made by the system are not affected."
                                                          : nxnet_status_str(g_np.avail),
                      th->text_secondary, NXC_ALIGN_CENTER);
    nxc_rect_t btn = nxc_rect(cx - 56.0f, card.y + card.h - 54.0f, 112.0f, NXGLASS_BUTTON_H);
    nxglass_button(c, btn, "Try Again", NXGLASS_BUTTON_DEFAULT, g_np.hover == NP_HIT_RETRY ? NXGLASS_HOVER : 0);
}

static nxc_rect_t np_retry_rect(nxc_rect_t a) {
    float w = a.w - 2.0f * NP_PAD;
    if (w > 440.0f) w = 440.0f;
    nxc_rect_t card = nxc_rect(a.x + (a.w - w) * 0.5f, a.y + 110.0f, w, 236.0f);
    return nxc_rect(card.x + card.w * 0.5f - 56.0f, card.y + card.h - 54.0f, 112.0f, NXGLASS_BUTTON_H);
}

/* A checkbox (nxglass has none yet; drawn locally, see report). */
static void np_checkbox(nxc_canvas_t *c, nxc_rect_t r, int on, const char *label) {
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t box = nxc_rect(r.x, r.y + (r.h - 16.0f) * 0.5f, 16.0f, 16.0f);
    if (on) {
        nxc_fill_rrect(c, box, 4.0f, th->accent);
        nxsym_draw(c, NXSYM_CHECK, box.x + 8.0f, box.y + 8.0f, 12.0f, th->text_on_accent, 0.0f);
    } else {
        nxc_fill_rrect(c, box, 4.0f, th->control_fill);
        nxc_stroke_rrect(c, box, 4.0f, 1.0f, th->separator);
    }
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(r.x + 24.0f, r.y, r.w - 24.0f, r.h), label,
                      th->text_primary, NXC_ALIGN_LEFT);
}

static void np_draw_join_sheet(nxc_canvas_t *c, nxc_rect_t s) {
    const nxui_theme_t *th = nxui_theme();
    np_symbol_tile(c, s.x + 20.0f, s.y + 22.0f, 40.0f, NXSYM_LOCK, 1);
    char title[64];
    uint32_t p = np_app(title, 0, sizeof(title), "Join \xE2\x80\x9C");
    p = np_app(title, p, sizeof(title), g_np.join_ssid);
    np_app(title, p, sizeof(title), "\xE2\x80\x9D");
    nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, s.w - 92.0f, 0 };
    nxui_text(c, NXUI_TEXT_HEADLINE, s.x + 72.0f, s.y + 38.0f, title, th->text_primary, &o);
    char sub[80];
    p = np_app(sub, 0, sizeof(sub), "Enter the ");
    p = np_app(sub, p, sizeof(sub), nxnet_security_str(g_np.join_sec));
    np_app(sub, p, sizeof(sub), " password.");
    nxui_text(c, NXUI_TEXT_FOOTNOTE, s.x + 72.0f, s.y + 56.0f, sub, th->text_secondary, &o);

    nxui_text(c, NXUI_TEXT_CAPTION, s.x + 20.0f, s.y + 96.0f, "Password", th->text_secondary, 0);
    char shown[NXNET_PASSPHRASE_MAX + 2];
    uint32_t n = np_strlen(g_np.pass);
    if (g_np.show_pass) np_strcpy(shown, g_np.pass, sizeof(shown));
    else { for (uint32_t i = 0; i < n && i + 1 < sizeof(shown); i++) shown[i] = '*'; shown[n < sizeof(shown) ? n : sizeof(shown) - 1] = '\0'; }
    nxc_rect_t fr = np_join_field(s);
    nxglass_field(c, fr, shown, "8 to 63 characters", 0, NXGLASS_FOCUSED);
    np_wipe(shown, sizeof(shown));
    if (g_np.sheet_err[0]) nxc_stroke_rrect(c, fr, th->radius_control, 1.5f, th->destructive);

    np_checkbox(c, np_join_show(s), g_np.show_pass, "Show password");
    if (g_np.sheet_err[0]) {
        nxui_text(c, NXUI_TEXT_FOOTNOTE, s.x + 20.0f, s.y + 186.0f, g_np.sheet_err, th->destructive, &o);
    } else if (n > 0 && n < NXNET_PASSPHRASE_MIN) {
        char hint[48];
        uint32_t hp = np_app_u(hint, 0, sizeof(hint), NXNET_PASSPHRASE_MIN - n);
        np_app(hint, hp, sizeof(hint), NXNET_PASSPHRASE_MIN - n == 1 ? " more character needed" : " more characters needed");
        nxui_text(c, NXUI_TEXT_FOOTNOTE, s.x + 20.0f, s.y + 186.0f, hint, th->text_tertiary, &o);
    }
    int vs = nxnet_validate_passphrase(g_np.pass, g_np.join_sec) == NXNET_OK;
    nxglass_button(c, np_sheet_cancel(s), "Cancel", NXGLASS_BUTTON_DEFAULT,
                   g_np.hover == NP_HIT_SHEET_CANCEL ? NXGLASS_HOVER : 0);
    nxglass_button(c, np_sheet_ok(s), "Join", NXGLASS_BUTTON_PRIMARY, vs ? 0 : NXGLASS_DISABLED);
}

static const char *const k_adv_labels[NP_F_COUNT] = { "IP Address", "Subnet Mask", "Router", "DNS Server" };
static const char *const k_adv_place[NP_F_COUNT] = { "192.168.1.20", "255.255.255.0", "192.168.1.1", "1.1.1.1" };

static nxnet_field_t np_field_id(int i) {
    switch (i) {
        case NP_F_ADDR:   return NXNET_FIELD_ADDRESS;
        case NP_F_MASK:   return NXNET_FIELD_NETMASK;
        case NP_F_ROUTER: return NXNET_FIELD_GATEWAY;
        default:          return NXNET_FIELD_DNS;
    }
}

static void np_draw_adv_sheet(nxc_canvas_t *c, nxc_rect_t s) {
    const nxui_theme_t *th = nxui_theme();
    const nxnet_interface_t *f = np_selected();
    char title[48];
    uint32_t p = np_app(title, 0, sizeof(title), "Advanced \xE2\x80\x94 ");
    np_app(title, p, sizeof(title), f ? nxnet_if_kind_str(f->kind) : "Network");
    nxui_text(c, NXUI_TEXT_HEADLINE, s.x + 20.0f, s.y + 32.0f, title, th->text_primary, 0);

    nxc_rect_t seg = np_adv_segment(s);
    nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(s.x + 20.0f, seg.y, 108.0f, seg.h), "Configure IPv4",
                      th->text_secondary, NXC_ALIGN_LEFT);
    static const char *const segs[2] = { "Using DHCP", "Manually" };
    nxglass_segmented(c, seg, segs, 2, g_np.adv_method, g_np.hover == NP_HIT_SEGMENT ? 1 - g_np.adv_method : -1);

    for (int i = 0; i < NP_F_COUNT; i++) {
        nxc_rect_t fr = np_adv_field(s, i);
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(s.x + 20.0f, fr.y, 108.0f, fr.h), k_adv_labels[i],
                          th->text_secondary, NXC_ALIGN_LEFT);
        uint32_t st = g_np.adv_method == 0 ? NXGLASS_DISABLED : 0;
        if (g_np.adv_method == 1 && g_np.focus == i) st |= NXGLASS_FOCUSED;
        nxc_save(c);
        if (g_np.adv_method == 0) nxc_set_alpha(c, 120);
        nxglass_field(c, fr, g_np.fields[i], g_np.adv_method == 1 ? k_adv_place[i] : "", 0, st);
        nxc_restore(c);
        if (g_np.adv_method == 1 && g_np.bad_field != NXNET_FIELD_NONE && g_np.bad_field == np_field_id(i))
            nxc_stroke_rrect(c, fr, th->radius_control, 1.5f, th->destructive);
    }
    if (g_np.sheet_err[0]) {
        nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, s.w - 40.0f, 0 };
        nxui_text(c, NXUI_TEXT_FOOTNOTE, s.x + 20.0f, s.y + s.h - 62.0f, g_np.sheet_err, th->destructive, &o);
    } else if (g_np.adv_method == 0) {
        nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, s.w - 40.0f, 0 };
        nxui_text(c, NXUI_TEXT_FOOTNOTE, s.x + 20.0f, s.y + s.h - 62.0f,
                  "Addresses are assigned automatically by the network.", th->text_tertiary, &o);
    }
    nxglass_button(c, np_sheet_cancel(s), "Cancel", NXGLASS_BUTTON_DEFAULT,
                   g_np.hover == NP_HIT_SHEET_CANCEL ? NXGLASS_HOVER : 0);
    nxglass_button(c, np_sheet_ok(s), "Apply", NXGLASS_BUTTON_PRIMARY, 0);
}

static void np_draw_sheet(nxc_canvas_t *c, nxc_rect_t a) {
    nxc_fill_rect(c, a, NXC_ARGB(nxui_appearance() == NXUI_APPEARANCE_DARK ? 110 : 60, 0, 0, 0));
    nxc_rect_t s = np_sheet_rect(a);
    nxglass_panel(c, s, 14.0f, NXGLASS_REGULAR, 3, 1);
    if (g_np.sheet == NP_SHEET_JOIN) np_draw_join_sheet(c, s);
    else np_draw_adv_sheet(c, s);
}

/* ============ Render ============ */

static void network_pane_render(nxc_canvas_t *c, nxc_rect_t area) {
    if (!c || area.w <= 0.0f || area.h <= 0.0f) return;
    g_np.area = area;
    np_maybe_refresh();
    const nxui_theme_t *th = nxui_theme();

    if (g_np.avail != NXNET_OK) {
        if (g_np.sheet != NP_SHEET_NONE) { np_wipe(g_np.pass, sizeof(g_np.pass)); g_np.sheet = NP_SHEET_NONE; }
        np_draw_unavailable(c, area);
        return;
    }

    np_layout_t L = np_layout(area);
    np_draw_title(c, &L);
    np_draw_interfaces(c, &L);

    const nxnet_interface_t *f = np_selected();
    if (f) {
        np_draw_header(c, &L, f);
        np_draw_details(c, &L, f);
        if (f->kind == NXNET_IF_WIFI && g_np.wifi_st == NXNET_OK) {
            np_draw_networks(c, &L);
            if (g_np.wifi.state == NXNET_WIFI_CONNECTED)
                nxglass_button(c, L.forget_btn, "Forget Network", NXGLASS_BUTTON_DEFAULT,
                               g_np.hover == NP_HIT_FORGET ? NXGLASS_HOVER : 0);
        } else {
            np_draw_ethernet_traffic(c, &L, f);
        }
        nxglass_button(c, L.adv_btn, "Advanced\xE2\x80\xA6", NXGLASS_BUTTON_DEFAULT,
                       g_np.hover == NP_HIT_ADVANCED ? NXGLASS_HOVER : 0);
    }

    if (g_np.banner[0] && nxui_time() < g_np.banner_until) {
        float w = nxui_text_width(NXUI_TEXT_FOOTNOTE, g_np.banner) + 28.0f;
        nxc_rect_t b = nxc_rect(L.title.x + L.title.w - w, L.title.y + 4.0f, w, 26.0f);
        nxc_color_t col = g_np.banner_err ? th->destructive : th->success;
        nxc_fill_rrect(c, b, 13.0f, NXC_WITH_ALPHA(col, 40));
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, b, g_np.banner, col, NXC_ALIGN_CENTER);
    }

    if (g_np.sheet != NP_SHEET_NONE) np_draw_sheet(c, area);
}

/* ============ Actions ============ */

static void np_close_sheet(void) {
    np_wipe(g_np.pass, sizeof(g_np.pass));
    g_np.sheet = NP_SHEET_NONE;
    g_np.sheet_err[0] = '\0';
    g_np.focus = -1;
    g_np.show_pass = 0;
}

static void np_open_join(const nxnet_wifi_network_t *n) {
    if (!n) return;
    if (n->current) return;
    if (n->security == NXNET_WIFI_SEC_OPEN) {
        nxnet_status_t st = nxnet_wifi_join(n->ssid, "", n->security);
        np_set_banner(st == NXNET_OK ? "Connected" : nxnet_status_str(st), st != NXNET_OK);
        np_refresh();
        return;
    }
    if (n->security == NXNET_WIFI_SEC_WEP) { np_set_banner("WEP networks are not supported (insecure)", 1); return; }
    np_close_sheet();
    np_strcpy(g_np.join_ssid, n->ssid, sizeof(g_np.join_ssid));
    g_np.join_sec = n->security;
    g_np.sheet = NP_SHEET_JOIN;
    g_np.focus = 0;
}

static void np_submit_join(void) {
    nxnet_status_t v = nxnet_validate_passphrase(g_np.pass, g_np.join_sec);
    if (v != NXNET_OK) {
        np_strcpy(g_np.sheet_err, "Password must be 8 to 63 characters.", sizeof(g_np.sheet_err));
        return;
    }
    nxnet_status_t st = nxnet_wifi_join(g_np.join_ssid, g_np.pass, g_np.join_sec);
    if (st == NXNET_OK) {
        np_close_sheet();
        np_set_banner("Connected", 0);
        np_refresh();
        return;
    }
    np_wipe(g_np.pass, sizeof(g_np.pass));
    np_strcpy(g_np.sheet_err, st == NXNET_ERR_AUTH ? "Incorrect password. Try again." : nxnet_status_str(st),
              sizeof(g_np.sheet_err));
}

static void np_open_advanced(void) {
    const nxnet_interface_t *f = np_selected();
    if (!f) return;
    np_close_sheet();
    g_np.sheet = NP_SHEET_ADVANCED;
    g_np.adv_if_index = f->index;
    g_np.adv_method = f->ipv4.method == NXNET_IPV4_MANUAL ? 1 : 0;
    g_np.bad_field = NXNET_FIELD_NONE;
    for (int i = 0; i < NP_F_COUNT; i++) g_np.fields[i][0] = '\0';
    if (f->state == NXNET_IF_CONNECTED) {
        nxnet_ipv4_format(f->ipv4.address, g_np.fields[NP_F_ADDR], NP_FIELD_CAP);
        nxnet_ipv4_format(f->ipv4.netmask, g_np.fields[NP_F_MASK], NP_FIELD_CAP);
        if (!nxnet_ipv4_is_zero(f->ipv4.gateway)) nxnet_ipv4_format(f->ipv4.gateway, g_np.fields[NP_F_ROUTER], NP_FIELD_CAP);
        if (f->ipv4.dns_count > 0) nxnet_ipv4_format(f->ipv4.dns[0], g_np.fields[NP_F_DNS], NP_FIELD_CAP);
    }
    g_np.focus = g_np.adv_method == 1 ? 0 : -1;
}

static void np_submit_advanced(void) {
    nxnet_ipv4_config_t cfg;
    for (uint32_t i = 0; i < sizeof(cfg); i++) ((uint8_t *)&cfg)[i] = 0;
    g_np.bad_field = NXNET_FIELD_NONE;
    g_np.sheet_err[0] = '\0';
    if (g_np.adv_method == 0) {
        cfg.method = NXNET_IPV4_DHCP;
    } else {
        cfg.method = NXNET_IPV4_MANUAL;
        static const char *const msg[NP_F_COUNT] = {
            "Enter a valid IP address (e.g. 192.168.1.20).",
            "Enter a valid subnet mask (e.g. 255.255.255.0).",
            "Enter a valid router address, or leave it empty.",
            "Enter a valid DNS server address, or leave it empty."
        };
        nxnet_ipv4_t *dst[NP_F_COUNT] = { &cfg.address, &cfg.netmask, &cfg.gateway, &cfg.dns[0] };
        for (int i = 0; i < NP_F_COUNT; i++) {
            int optional = (i == NP_F_ROUTER || i == NP_F_DNS);
            if (optional && g_np.fields[i][0] == '\0') continue;
            if (nxnet_ipv4_parse(g_np.fields[i], dst[i]) != NXNET_OK) {
                g_np.bad_field = np_field_id(i);
                g_np.focus = i;
                np_strcpy(g_np.sheet_err, msg[i], sizeof(g_np.sheet_err));
                return;
            }
        }
        cfg.dns_count = g_np.fields[NP_F_DNS][0] ? 1u : 0u;
        nxnet_field_t bad = NXNET_FIELD_NONE;
        if (nxnet_ipv4_config_validate(&cfg, &bad) != NXNET_OK) {
            g_np.bad_field = bad;
            switch (bad) {
                case NXNET_FIELD_ADDRESS: g_np.focus = NP_F_ADDR;
                    np_strcpy(g_np.sheet_err, "The IP address is not a usable host address for this subnet.", sizeof(g_np.sheet_err)); break;
                case NXNET_FIELD_NETMASK: g_np.focus = NP_F_MASK;
                    np_strcpy(g_np.sheet_err, "The subnet mask must be contiguous (e.g. 255.255.255.0).", sizeof(g_np.sheet_err)); break;
                case NXNET_FIELD_GATEWAY: g_np.focus = NP_F_ROUTER;
                    np_strcpy(g_np.sheet_err, "The router must be a different address in the same subnet.", sizeof(g_np.sheet_err)); break;
                default: g_np.focus = NP_F_DNS;
                    np_strcpy(g_np.sheet_err, "The DNS server must be a unicast address.", sizeof(g_np.sheet_err)); break;
            }
            return;
        }
    }
    nxnet_status_t st = nxnet_ipv4_configure(g_np.adv_if_index, &cfg);
    if (st != NXNET_OK) {
        np_strcpy(g_np.sheet_err, nxnet_status_str(st), sizeof(g_np.sheet_err));
        return;
    }
    np_close_sheet();
    np_set_banner("Settings applied", 0);
    np_refresh();
}

/* ============ Input ============ */

static int np_hit_test(int32_t ax, int32_t ay) {
    nxc_rect_t a = g_np.area;
    if (g_np.avail != NXNET_OK) return nxglass_hit(np_retry_rect(a), ax, ay) ? NP_HIT_RETRY : NP_HIT_NONE;
    if (g_np.sheet != NP_SHEET_NONE) {
        nxc_rect_t s = np_sheet_rect(a);
        if (nxglass_hit(np_sheet_ok(s), ax, ay)) return NP_HIT_SHEET_OK;
        if (nxglass_hit(np_sheet_cancel(s), ax, ay)) return NP_HIT_SHEET_CANCEL;
        if (g_np.sheet == NP_SHEET_JOIN) {
            if (nxglass_hit(np_join_show(s), ax, ay)) return NP_HIT_SHOW_PASS;
            if (nxglass_hit(np_join_field(s), ax, ay)) return NP_HIT_SHEET_FIELD;
        } else {
            if (nxglass_hit(np_adv_segment(s), ax, ay)) return NP_HIT_SEGMENT;
            for (int i = 0; i < NP_F_COUNT; i++)
                if (nxglass_hit(np_adv_field(s, i), ax, ay)) return NP_HIT_SHEET_FIELD + i;
        }
        return NP_HIT_NONE;
    }
    np_layout_t L = np_layout(a);
    for (uint32_t i = 0; i < g_np.nif; i++)
        if (nxglass_hit(np_if_row(&L, i), ax, ay)) return NP_HIT_IF_BASE + (int)i;
    const nxnet_interface_t *f = np_selected();
    if (!f) return NP_HIT_NONE;
    if (nxglass_hit(L.adv_btn, ax, ay)) return NP_HIT_ADVANCED;
    if (f->kind == NXNET_IF_WIFI && g_np.wifi_st == NXNET_OK) {
        if (nxglass_hit(L.toggle, ax, ay)) return NP_HIT_WIFI_TOGGLE;
        if (g_np.wifi.state == NXNET_WIFI_CONNECTED && nxglass_hit(L.forget_btn, ax, ay)) return NP_HIT_FORGET;
        if (g_np.wifi.state != NXNET_WIFI_OFF) {
            if (nxglass_hit(L.scan_btn, ax, ay)) return NP_HIT_SCAN;
            uint32_t vis = np_visible_nets(&L);
            for (uint32_t i = 0; i < vis; i++)
                if (nxglass_hit(np_net_row(&L, i), ax, ay)) return NP_HIT_NET_BASE + (int)i;
        }
    }
    return NP_HIT_NONE;
}

static void np_activate(int hit) {
    if (hit >= NP_HIT_IF_BASE && hit < NP_HIT_IF_BASE + (int)g_np.nif) {
        g_np.sel = hit - NP_HIT_IF_BASE;
        g_np.sel_index = g_np.ifs[g_np.sel].index;
        g_np.toggle_t = g_np.ifs[g_np.sel].state == NXNET_IF_DISABLED ? 0.0f : 1.0f;
        return;
    }
    if (hit >= NP_HIT_NET_BASE && hit < NP_HIT_NET_BASE + (int)g_np.nnets) {
        np_open_join(&g_np.nets[hit - NP_HIT_NET_BASE]);
        return;
    }
    if (hit >= NP_HIT_SHEET_FIELD && hit < NP_HIT_SHEET_FIELD + NP_F_COUNT) {
        if (g_np.sheet == NP_SHEET_ADVANCED && g_np.adv_method == 1) g_np.focus = hit - NP_HIT_SHEET_FIELD;
        else if (g_np.sheet == NP_SHEET_JOIN) g_np.focus = 0;
        return;
    }
    nxnet_status_t st;
    switch (hit) {
        case NP_HIT_WIFI_TOGGLE:
            st = nxnet_wifi_set_power(g_np.wifi.state == NXNET_WIFI_OFF ? 1 : 0);
            if (st != NXNET_OK) np_set_banner(nxnet_status_str(st), 1);
            np_refresh();
            break;
        case NP_HIT_SCAN:
            st = nxnet_wifi_scan();
            if (st != NXNET_OK) np_set_banner(nxnet_status_str(st), 1);
            np_refresh();
            break;
        case NP_HIT_FORGET: {
            char ssid[NXNET_SSID_MAX + 1];
            np_strcpy(ssid, g_np.wifi.ssid, sizeof(ssid));
            st = nxnet_wifi_forget(ssid);
            np_set_banner(st == NXNET_OK ? "Network forgotten" : nxnet_status_str(st), st != NXNET_OK);
            np_refresh();
            break;
        }
        case NP_HIT_ADVANCED: np_open_advanced(); break;
        case NP_HIT_RETRY:
            nxnet_init();
            np_refresh();
            break;
        case NP_HIT_SHEET_CANCEL: np_close_sheet(); break;
        case NP_HIT_SHEET_OK:
            if (g_np.sheet == NP_SHEET_JOIN) np_submit_join(); else np_submit_advanced();
            break;
        case NP_HIT_SHOW_PASS: g_np.show_pass = !g_np.show_pass; break;
        case NP_HIT_SEGMENT: {
            g_np.adv_method = 1 - g_np.adv_method;
            g_np.bad_field = NXNET_FIELD_NONE;
            g_np.sheet_err[0] = '\0';
            g_np.focus = g_np.adv_method == 1 ? 0 : -1;
            break;
        }
        default: break;
    }
}

static int network_pane_mouse(int32_t lx, int32_t ly, uint8_t b, uint8_t pb) {
    int32_t ax = (int32_t)g_np.area.x + lx, ay = (int32_t)g_np.area.y + ly;
    int hit = np_hit_test(ax, ay);
    g_np.hover = hit;
    int pressed = (b & 1) && !(pb & 1);
    if (!pressed) return hit != NP_HIT_NONE;
    if (g_np.sheet == NP_SHEET_ADVANCED && hit == NP_HIT_SEGMENT) {
        int seg = nxglass_segment_at(np_adv_segment(np_sheet_rect(g_np.area)), 2, ax, ay);
        if (seg >= 0 && seg != g_np.adv_method) np_activate(NP_HIT_SEGMENT);
        return 1;
    }
    if (hit == NP_HIT_NONE) return g_np.sheet != NP_SHEET_NONE;   /* sheet is modal */
    np_activate(hit);
    return 1;
}

/* PS/2 set-1 to ASCII, used when the host passes ascii == 0. */
static char np_scancode_ascii(uint8_t sc, int shift) {
    static const char lo[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./";
    static const char hi[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?";
    if (sc == 0x39) return ' ';
    if (sc >= sizeof(lo) - 1) return 0;
    return shift ? hi[sc] : lo[sc];
}

static void np_field_type(char *buf, uint32_t cap, char ch, int ipv4_only) {
    uint32_t n = np_strlen(buf);
    if (ipv4_only && !((ch >= '0' && ch <= '9') || ch == '.')) return;
    if ((uint8_t)ch < 0x20 || (uint8_t)ch > 0x7E) return;
    if (n + 1 >= cap) return;
    buf[n] = ch;
    buf[n + 1] = '\0';
}

static int network_pane_key(uint8_t sc, uint8_t pressed, char ascii) {
    if (sc == 0x2A || sc == 0x36) { g_np.shift = pressed ? 1 : 0; return 0; }
    if (!pressed || g_np.sheet == NP_SHEET_NONE) return 0;
    if (sc == 0x01) { np_close_sheet(); return 1; }                       /* Esc */
    if (sc == 0x1C) { np_activate(NP_HIT_SHEET_OK); return 1; }           /* Enter */
    if (g_np.sheet == NP_SHEET_ADVANCED) {
        if (g_np.adv_method != 1) return 1;
        if (sc == 0x0F) { g_np.focus = (g_np.focus + (g_np.shift ? NP_F_COUNT - 1 : 1)) % NP_F_COUNT; return 1; }
        if (g_np.focus < 0 || g_np.focus >= NP_F_COUNT) return 1;
        char *buf = g_np.fields[g_np.focus];
        if (sc == 0x0E) { uint32_t n = np_strlen(buf); if (n) buf[n - 1] = '\0'; }
        else {
            char ch = ascii ? ascii : np_scancode_ascii(sc, g_np.shift);
            if (ch) np_field_type(buf, NP_FIELD_CAP, ch, 1);
        }
        if (g_np.bad_field == np_field_id(g_np.focus)) { g_np.bad_field = NXNET_FIELD_NONE; g_np.sheet_err[0] = '\0'; }
        return 1;
    }
    /* Join sheet: password field */
    if (sc == 0x0E) { uint32_t n = np_strlen(g_np.pass); if (n) g_np.pass[n - 1] = '\0'; }
    else if (sc != 0x0F) {
        char ch = ascii ? ascii : np_scancode_ascii(sc, g_np.shift);
        if (ch) np_field_type(g_np.pass, NXNET_PASSPHRASE_MAX + 1, ch, 0);
    }
    g_np.sheet_err[0] = '\0';
    return 1;
}

static void network_pane_on_show(void) {
    g_np.hover = NP_HIT_NONE;
    g_np.ready = 0;
    np_refresh();
    const nxnet_interface_t *f = np_selected();
    g_np.toggle_t = (f && f->state == NXNET_IF_DISABLED) ? 0.0f : 1.0f;
}

const settings_pane_t settings_pane_network = {
    "network", "Network", network_pane_render, network_pane_mouse, network_pane_key, network_pane_on_show,
    0   /* no scrolling: the pane fits its area */
};

/* ============ Entry points for other shell modules ============ */

int network_pane_begin_join(const char *ssid) {
    if (nxnet_validate_ssid(ssid) != NXNET_OK) return -1;
    np_refresh();
    if (g_np.avail != NXNET_OK) return -1;
    for (uint32_t i = 0; i < g_np.nif; i++) {
        if (g_np.ifs[i].kind == NXNET_IF_WIFI) { g_np.sel = (int)i; g_np.sel_index = g_np.ifs[i].index; }
    }
    for (uint32_t i = 0; i < g_np.nnets; i++) {
        if (np_streq(g_np.nets[i].ssid, ssid)) { np_open_join(&g_np.nets[i]); return 0; }
    }
    return -1;
}

/* ============ Host preview only ============ */
#ifdef NX_HOST_PREVIEW
/*
 * Review path until the Settings area registers this pane: a plain
 * window hosting the pane beside a stand-in sidebar, and a window that
 * shows the menu bar network dropdown. Reach them from preview scripts
 * with "call net_preview_open" etc. Not compiled into the OS.
 */
#include "app_view.h"
#include "desktop_shell.h"

static uint32_t g_prev_win, g_prev_menu_win;
#define NP_PREV_SIDEBAR 200.0f

static nxc_rect_t np_prev_area(const desktop_window_t *win) {
    nxc_rect_t r = app_view_content_rect(win);
    return nxc_rect(r.x + NP_PREV_SIDEBAR, r.y, r.w - NP_PREV_SIDEBAR, r.h);
}

static void np_prev_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    if (!win) return;
    nxc_canvas_t *c = nxui_canvas();
    nxc_rect_t r = app_view_content_rect(win);
    nxc_fill_rect(c, r, nxui_theme()->window_bg);
    nxc_rect_t sb = nxc_rect(r.x, r.y, NP_PREV_SIDEBAR, r.h);
    nxglass_sidebar(c, sb);
    static const char *const items[] = { "Wi-Fi", "Network", "Bluetooth", "Appearance", "Display", "Sound" };
    static const int icons[] = { NXSYM_WIFI, NXSYM_ETHERNET, NXSYM_BLUETOOTH, NXSYM_SUN, NXSYM_BRIGHTNESS, NXSYM_VOLUME };
    nxglass_field(c, nxc_rect(sb.x + 12.0f, sb.y + 14.0f, sb.w - 24.0f, NXGLASS_FIELD_H), "", "Search", 1, 0);
    for (int i = 0; i < 6; i++)
        nxglass_row(c, nxc_rect(sb.x + 8.0f, sb.y + 56.0f + (float)i * NXGLASS_ROW_H, sb.w - 16.0f, NXGLASS_ROW_H - 4.0f),
                    icons[i], items[i], 0, i == 1 ? NXGLASS_SELECTED : 0);
    network_pane_render(c, np_prev_area(win));
}

static void np_prev_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)win; (void)ctx;
    network_pane_mouse(lx - (int32_t)NP_PREV_SIDEBAR, ly, b, pb);
}

static void np_prev_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx) {
    (void)win; (void)ctx;
    network_pane_key(sc, pressed, 0);
}

void net_preview_open(void) {
    if (g_prev_win) { desktop_focus_window(g_prev_win); return; }
    g_prev_win = desktop_create_window("System Settings", 210, 70, 860, 560);
    if (!g_prev_win) { nx_debug_print("[NETPREVIEW] window create failed\n"); return; }
    desktop_set_window_render(g_prev_win, np_prev_render, 0);
    desktop_set_window_mouse_handler(g_prev_win, np_prev_mouse, 0);
    desktop_set_window_key_handler(g_prev_win, np_prev_key, 0);
    network_pane_on_show();
}

void net_preview_select_wifi(void) {
    for (uint32_t i = 0; i < g_np.nif; i++)
        if (g_np.ifs[i].kind == NXNET_IF_WIFI) np_activate(NP_HIT_IF_BASE + (int)i);
}

void net_preview_join(void) { network_pane_begin_join("Neolyx-5G"); }
void net_preview_advanced(void) { np_open_advanced(); }
void net_preview_unavailable(void) { np_close_sheet(); nxnet_preview_set_unavailable(); np_refresh(); }
void net_preview_wifi_off(void) { nxnet_preview_wifi_off(); np_refresh(); g_np.toggle_t = 0.0f; }
void net_preview_light(void) { nxui_set_appearance(NXUI_APPEARANCE_LIGHT); }

static void np_prev_menu_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    if (!win) return;
    nxc_rect_t r = app_view_content_rect(win);
    float w = 0, h = 0;
    network_indicator_menu_size(&w, &h);
    nxc_fill_rect(nxui_canvas(), r, NXC_ARGB(0, 0, 0, 0));
    network_indicator_draw_menu(nxui_canvas(), nxc_rect(r.x + 10.0f, r.y + 10.0f, w, h), 1);
}

static void np_prev_menu_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)win; (void)ctx;
    network_indicator_menu_mouse((float)lx - 10.0f, (float)ly - 10.0f, b, pb);
}

void net_preview_menu(void) {
    if (g_prev_menu_win) { desktop_focus_window(g_prev_menu_win); return; }
    float w = 0, h = 0;
    network_indicator_menu_size(&w, &h);
    g_prev_menu_win = desktop_create_window("Network Menu", 1280 - (int32_t)w - 60, 40, (uint32_t)w + 20, (uint32_t)h + 20);
    if (!g_prev_menu_win) return;
    desktop_set_window_render(g_prev_menu_win, np_prev_menu_render, 0);
    desktop_set_window_mouse_handler(g_prev_menu_win, np_prev_menu_mouse, 0);
}
#endif /* NX_HOST_PREVIEW */
