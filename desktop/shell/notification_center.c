/*
 * NeolyxOS Notification Center Implementation
 * 
 * Slide-in notification panel from top-right.
 * Displays system and app notifications with dismiss.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/notification_center.h"
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxsyscall.h"
#include <stddef.h>

/* ============ Static State ============ */

static notification_center_state_t g_nc;
static int g_nc_initialized = 0;
static uint32_t g_next_notification_id = 1;

/* ============ String Helpers ============ */

static void nc_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}


/* ============ Initialization ============ */

void notification_center_init(uint32_t screen_w, uint32_t screen_h) {
    if (g_nc_initialized) return;
    
    g_nc.screen_width = screen_w;
    g_nc.screen_height = screen_h;
    g_nc.visible = 0;
    g_nc.count = 0;
    g_nc.hover_idx = -1;
    g_nc.scroll_offset = 0;
    g_nc.slide_progress = 0;
    
    /* Position: top-right, left of Control Center */
    g_nc.width = NC_WIDTH;
    g_nc.height = NC_HEIGHT;
    g_nc.x = screen_w - NC_WIDTH - NC_MARGIN - 360;  /* Left of CC */
    g_nc.y = 32;
    
    /* Add sample notification */
    notification_push("System", "Welcome to NeolyxOS", "Your desktop is ready.", 0xFF007AFF);
    
    g_nc_initialized = 1;
}

/* ============ Toggle ============ */

void notification_center_toggle(void) {
    g_nc.visible = !g_nc.visible;
    if (g_nc.visible) g_nc.slide_progress = 0;
}

void notification_center_show(void) {
    g_nc.visible = 1;
    g_nc.slide_progress = 0;
}

void notification_center_hide(void) {
    g_nc.visible = 0;
}

int notification_center_is_visible(void) {
    return g_nc.visible;
}

/* ============ Notification Management ============ */

uint32_t notification_push(const char *app_name, const char *title, const char *message, uint32_t icon_color) {
    if (g_nc.count >= NC_MAX_NOTIFICATIONS) {
        /* Remove oldest */
        for (int i = 0; i < g_nc.count - 1; i++) {
            g_nc.notifications[i] = g_nc.notifications[i + 1];
        }
        g_nc.count--;
    }
    
    notification_t *notif = &g_nc.notifications[g_nc.count];
    notif->id = g_next_notification_id++;
    nc_strcpy(notif->app_name, app_name, 32);
    nc_strcpy(notif->title, title, 64);
    nc_strcpy(notif->message, message, 128);
    notif->icon_color = icon_color ? icon_color : NC_ACCENT;
    notif->timestamp = nx_gettime();
    notif->read = 0;
    notif->dismissed = 0;
    
    g_nc.count++;
    return notif->id;
}

void notification_dismiss(uint32_t id) {
    for (int i = 0; i < g_nc.count; i++) {
        if (g_nc.notifications[i].id == id) {
            /* Shift remaining */
            for (int j = i; j < g_nc.count - 1; j++) {
                g_nc.notifications[j] = g_nc.notifications[j + 1];
            }
            g_nc.count--;
            return;
        }
    }
}

void notification_clear_all(void) {
    g_nc.count = 0;
}

int notification_get_count(void) {
    return g_nc.count;
}

/* ============ Rendering ============ */

/* ============ Animation ============ */

void notification_center_tick(void) {
    if (!g_nc_initialized) return;
    
    if (g_nc.visible && g_nc.slide_progress < 100) {
        g_nc.slide_progress += 10;
        if (g_nc.slide_progress > 100) g_nc.slide_progress = 100;
    }
}

/* ============ Input Handling ============ */

int notification_center_handle_input(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_nc.visible || !g_nc_initialized) return 0;
    
    /* Check if outside panel */
    if (mouse_x < g_nc.x || mouse_x > g_nc.x + g_nc.width ||
        mouse_y < g_nc.y || mouse_y > g_nc.y + g_nc.height) {
        if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
            notification_center_hide();
            return 1;
        }
        return 0;
    }
    
    int rel_x = mouse_x - g_nc.x;
    int rel_y = mouse_y - g_nc.y;
    
    /* Update hover */
    g_nc.hover_idx = -1;
    if (rel_y >= 40 && g_nc.count > 0) {
        int idx = (rel_y - 40 + g_nc.scroll_offset) / (NC_NOTIF_HEIGHT + NC_NOTIF_GAP);
        if (idx >= 0 && idx < g_nc.count) {
            g_nc.hover_idx = idx;
        }
    }
    
    /* Click handling */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        /* Clear all button */
        if (rel_y < 40 && rel_x > g_nc.width - 80) {
            notification_clear_all();
            return 1;
        }
        
        /* Dismiss notification (X button) */
        if (g_nc.hover_idx >= 0 && rel_x > g_nc.width - 48) {
            notification_dismiss(g_nc.notifications[g_nc.hover_idx].id);
            return 1;
        }
        
        /* Mark as read on click */
        if (g_nc.hover_idx >= 0) {
            g_nc.notifications[g_nc.hover_idx].read = 1;
            return 1;
        }
    }
    
    return 1;
}

notification_center_state_t* notification_center_get_state(void) {
    return &g_nc;
}

/* ============ Canvas renderer ============ */

static float nc_anim = 0.0f;

/* Header row height; the first card starts here (input uses 40 too). */
#define NCL_HEADER_H 40.0f

/* "now" / "2m" / "1h" / "3d" age of a notification. */
static void nc_age(uint64_t ts, char *out, int cap) {
    if (!out || cap < 4) return;
    uint64_t now = nx_gettime();
    uint64_t s = now > ts ? (now - ts) / 1000u : 0;
    if (s < 60) { out[0] = 'n'; out[1] = 'o'; out[2] = 'w'; out[3] = '\0'; return; }
    char unit = s < 3600 ? 'm' : (s < 86400 ? 'h' : 'd');
    uint64_t v = s < 3600 ? s / 60 : (s < 86400 ? s / 3600 : s / 86400);
    char tmp[12];
    int k = 0, n = 0;
    do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 11);
    while (k > 0 && n < cap - 2) out[n++] = tmp[--k];
    out[n++] = unit;
    out[n] = '\0';
}

void notification_center_render(nxc_canvas_t *c) {
    if (!c || !g_nc_initialized) return;
    nc_anim = nxui_approach(nc_anim, g_nc.visible ? 1.0f : 0.0f, 0.06f, nxui_frame_dt());
    if (nc_anim < 0.01f) return;
    const nxglass_tokens_t *t = nxglass_tokens();
    float ease = nxui_ease_out_cubic(nc_anim);
    /* Resting rect + translation: the cached glass is reused while sliding. */
    nxc_rect_t panel = nxc_rect((float)g_nc.x, (float)g_nc.y, (float)g_nc.width, (float)g_nc.height);
    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(ease * 255.0f));
    nxc_translate(c, 0.0f, -(1.0f - ease) * 12.0f);
    nxglass_panel(c, panel, t->radius_panel + 4.0f, NXGLASS_REGULAR, 2, 0);

    /* Header */
    nxui_text_in_rect(c, NXUI_TEXT_TITLE3, nxc_rect(panel.x + 18.0f, panel.y + 4.0f, 200.0f, NCL_HEADER_H - 4.0f),
                      "Notifications", t->label, NXC_ALIGN_LEFT);
    if (g_nc.count > 0) {
        nxc_rect_t clr = nxc_rect(panel.x + panel.w - 80.0f, panel.y + 10.0f, 66.0f, 22.0f);
        nxglass_button(c, clr, "Clear all", NXGLASS_BUTTON_DEFAULT, 0);
    }

    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)panel.x, (int32_t)(panel.y + NCL_HEADER_H), g_nc.width, g_nc.height - 44));
    float y = panel.y + NCL_HEADER_H - (float)g_nc.scroll_offset;
    for (int i = 0; i < g_nc.count; i++) {
        const notification_t *nt = &g_nc.notifications[i];
        if (nt->dismissed) continue;
        nxc_rect_t card = nxc_rect(panel.x + 10.0f, y, panel.w - 20.0f, (float)NC_NOTIF_HEIGHT);
        int hover = i == g_nc.hover_idx;
        nxglass_card(c, card, t->radius_card);
        if (hover) nxc_fill_rrect(c, card, t->radius_card, t->fill);
        nxapp_icon_draw(c, nxapp_icon_from_name(nt->app_name), card.x + 12.0f, card.y + 16.0f, 38.0f, 255);
        float tx = card.x + 62.0f, tw = card.w - 62.0f - 40.0f;
        nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(tx, card.y + 9.0f, tw, 16.0f), nt->app_name,
                          t->label_secondary, NXC_ALIGN_LEFT);
        nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(tx, card.y + 26.0f, tw + 26.0f, 20.0f), nt->title,
                          t->label, NXC_ALIGN_LEFT);
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(tx, card.y + 46.0f, tw + 26.0f, 18.0f), nt->message,
                          t->label_secondary, NXC_ALIGN_LEFT);
        if (hover) {
            nxglass_icon_button(c, nxc_rect(card.x + card.w - 30.0f, card.y + 8.0f, 20.0f, 20.0f), NXSYM_CLOSE, NXGLASS_HOVER);
        } else {
            char age[8];
            nc_age(nt->timestamp, age, (int)sizeof(age));
            nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(card.x + card.w - 56.0f, card.y + 9.0f, 30.0f, 16.0f), age,
                              t->label_tertiary, NXC_ALIGN_RIGHT);
            if (!nt->read) nxc_fill_circle(c, card.x + card.w - 14.0f, card.y + 17.0f, 4.0f, t->accent);
        }
        y += NC_NOTIF_HEIGHT + NC_NOTIF_GAP;
    }
    if (g_nc.count == 0) {
        float cy = panel.y + panel.h * 0.42f;
        nxc_fill_circle(c, panel.x + panel.w * 0.5f, cy, 30.0f, t->fill);
        nxsym_draw(c, NXSYM_BELL, panel.x + panel.w * 0.5f, cy, 30.0f, t->label_tertiary, 0.0f);
        nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(panel.x, cy + 42.0f, panel.w, 20.0f), "No notifications",
                          t->label_secondary, NXC_ALIGN_CENTER);
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(panel.x, cy + 62.0f, panel.w, 18.0f),
                          "New alerts from apps and the system appear here.", t->label_tertiary, NXC_ALIGN_CENTER);
    }
    nxc_restore(c);
    nxc_restore(c);
}
