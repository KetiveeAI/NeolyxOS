/*
 * NeolyxOS Keyboard OSD (On-Screen Display)
 * 
 * Shows keyboard lock indicators (Caps Lock, Num Lock)
 * and keyboard shortcut hints.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/keyboard_osd.h"
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxsyscall.h"

/* OSD State */
static keyboard_osd_state_t g_kbd_osd = {0};
static int g_kbd_osd_initialized = 0;

/* Configuration */
#define KBD_OSD_WIDTH       240
#define KBD_OSD_HEIGHT      56
#define KBD_OSD_MARGIN      20
#define KBD_OSD_DURATION    2000    /* 2 seconds */
#define KBD_OSD_FADE_TIME   500     /* 0.5 second fade */
#define KBD_OSD_RADIUS      10

/* Colors */
#define KBD_BG_COLOR        0xD0252530
#define KBD_TEXT_COLOR      0xFFFFFFFF
#define KBD_ICON_ON         0xFF4AE04A  /* Green for ON */
#define KBD_ICON_OFF        0xFF505050  /* Gray for OFF */

/* Safe string copy */
static void kbd_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Initialize keyboard OSD */
void keyboard_osd_init(uint32_t screen_w, uint32_t screen_h) {
    g_kbd_osd.visible = 0;
    g_kbd_osd.type = KBD_OSD_NONE;
    g_kbd_osd.text[0] = '\0';
    g_kbd_osd.show_time = 0;
    g_kbd_osd.opacity = 255;
    
    /* Centered in the upper third, just below the appearance HUD: clear
     * of the menu bar, the search bar and the right-side panels. */
    g_kbd_osd.x = ((int32_t)screen_w - KBD_OSD_WIDTH) / 2;
    g_kbd_osd.y = (int32_t)(screen_h / 4) + 72;
    g_kbd_osd.width = KBD_OSD_WIDTH;
    g_kbd_osd.height = KBD_OSD_HEIGHT;
    
    g_kbd_osd_initialized = 1;
}

/* Show Caps Lock indicator */
void keyboard_osd_caps_lock(int on) {
    if (!g_kbd_osd_initialized) return;
    
    g_kbd_osd.visible = 1;
    g_kbd_osd.type = KBD_OSD_CAPS_LOCK;
    kbd_strcpy(g_kbd_osd.text, on ? "CAPS LOCK ON" : "CAPS LOCK OFF", 64);
    g_kbd_osd.show_time = nx_gettime();
    g_kbd_osd.opacity = 255;
}

/* Show Num Lock indicator */
void keyboard_osd_num_lock(int on) {
    if (!g_kbd_osd_initialized) return;
    
    g_kbd_osd.visible = 1;
    g_kbd_osd.type = KBD_OSD_NUM_LOCK;
    kbd_strcpy(g_kbd_osd.text, on ? "NUM LOCK ON" : "NUM LOCK OFF", 64);
    g_kbd_osd.show_time = nx_gettime();
    g_kbd_osd.opacity = 255;
}

/* Show keyboard shortcut hint */
void keyboard_osd_shortcut(const char *keys, const char *action) {
    if (!g_kbd_osd_initialized) return;
    
    g_kbd_osd.visible = 1;
    g_kbd_osd.type = KBD_OSD_SHORTCUT;
    
    /* Build shortcut string */
    char *dst = g_kbd_osd.text;
    int pos = 0;
    
    /* Keys */
    if (keys) {
        while (keys[pos] && pos < 30) {
            dst[pos] = keys[pos];
            pos++;
        }
    }
    
    /* Separator */
    if (action && pos < 60) {
        dst[pos++] = ' ';
        dst[pos++] = '-';
        dst[pos++] = ' ';
        
        /* Action */
        int j = 0;
        while (action[j] && pos < 63) {
            dst[pos++] = action[j++];
        }
    }
    dst[pos] = '\0';
    
    g_kbd_osd.show_time = nx_gettime();
    g_kbd_osd.opacity = 255;
}

/* Hide keyboard OSD */
void keyboard_osd_hide(void) {
    g_kbd_osd.visible = 0;
    g_kbd_osd.opacity = 0;
}

/* Check if visible */
int keyboard_osd_is_visible(void) {
    return g_kbd_osd.visible;
}

/* Update state (call each frame) */
void keyboard_osd_tick(void) {
    if (!g_kbd_osd.visible) return;
    
    uint64_t now = nx_gettime();
    uint64_t elapsed = now - g_kbd_osd.show_time;
    
    if (elapsed > KBD_OSD_DURATION) {
        uint64_t fade_elapsed = elapsed - KBD_OSD_DURATION;
        
        if (fade_elapsed >= KBD_OSD_FADE_TIME) {
            g_kbd_osd.visible = 0;
            g_kbd_osd.opacity = 0;
        } else {
            g_kbd_osd.opacity = 255 - (uint8_t)((fade_elapsed * 255) / KBD_OSD_FADE_TIME);
        }
    }
}

/* ============ Help Overlay (Full-Screen Shortcut Help) ============ */

/* Help overlay state */
static int g_help_visible = 0;
static uint32_t g_help_screen_w = 0;
static uint32_t g_help_screen_h = 0;

/* Shortcut definitions */
typedef struct {
    const char *keys;
    const char *action;
} shortcut_entry_t;

static const shortcut_entry_t g_shortcuts[] = {
    {"NX", "App Drawer"},
    {"NX + Space", "Anveshan Search"},
    {"NX + Tab", "Switch Windows"},
    {"Caps Lock", "Toggle Caps Lock"},
    {"Num Lock", "Toggle Num Lock"},
    {"Ctrl + C", "Copy"},
    {"Ctrl + V", "Paste"},
    {"Ctrl + X", "Cut"},
    {"Ctrl + Z", "Undo"},
    {"Ctrl + S", "Save"},
    {"Ctrl + A", "Select All"},
    {"Alt + F4", "Close Window"},
    {NULL, NULL}
};

void keyboard_osd_help_show(void) {
    g_help_visible = 1;
    g_help_screen_w = g_kbd_osd.width > 100 ? 1920 : 1920;  /* Use stored screen size */
    g_help_screen_h = 1080;
}

void keyboard_osd_help_hide(void) {
    g_help_visible = 0;
}

int keyboard_osd_help_visible(void) {
    return g_help_visible;
}

/* ============ Canvas renderer ============ */

/* Lock / shortcut HUD on ultrathin glass (cached panel, faded by alpha). */
void keyboard_osd_render(nxc_canvas_t *c) {
    if (!c || !g_kbd_osd.visible || g_kbd_osd.opacity == 0) return;
    nxc_rect_t box = nxc_rect((float)g_kbd_osd.x, (float)g_kbd_osd.y, (float)g_kbd_osd.width, (float)g_kbd_osd.height);
    int lock = g_kbd_osd.type == KBD_OSD_CAPS_LOCK || g_kbd_osd.type == KBD_OSD_NUM_LOCK;
    const char *title = g_kbd_osd.text;
    const char *sub = 0;
    if (g_kbd_osd.type == KBD_OSD_CAPS_LOCK) {
        int on = g_kbd_osd.text[10] == 'O' && g_kbd_osd.text[11] == 'N';   /* "CAPS LOCK ON" */
        title = "Caps Lock";
        sub = on ? "On" : "Off";
    } else if (g_kbd_osd.type == KBD_OSD_NUM_LOCK) {
        int on = g_kbd_osd.text[9] == 'O' && g_kbd_osd.text[10] == 'N';    /* "NUM LOCK ON" */
        title = "Num Lock";
        sub = on ? "On" : "Off";
    }
    nxglass_hud(c, box, lock ? NXSYM_LOCK : NXSYM_KEYBOARD, 0.0f, title, sub, -1.0f, 0, g_kbd_osd.opacity);
}

/* A keycap on glass: a raised glass button with a bottom lip. */
static void kbd_keycap(nxc_canvas_t *c, nxc_rect_t key, const char *label) {
    const nxglass_tokens_t *t = nxglass_tokens();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    nxc_fill_rrect(c, nxc_rect(key.x, key.y + 1.5f, key.w, key.h), t->radius_small,
                   light ? NXC_ARGB(40, 0, 0, 0x10) : NXC_ARGB(80, 0, 0, 0x08));
    nxglass_button(c, key, label, NXGLASS_BUTTON_DEFAULT, 0);
}

/* Full-screen shortcut sheet: scrim, centered glass card (cached). Also
 * the hook for the NXGlass design gallery, drawn last in the frame. */
void keyboard_osd_help_render(nxc_canvas_t *c) {
    if (!c) return;
    if (g_help_visible) {
        const nxglass_tokens_t *t = nxglass_tokens();
        float sw = (float)nxui_width(), sh = (float)nxui_height();
        nxglass_scrim(c, nxc_rect(0, 0, sw, sh), 1.0f);
        int count = 0;
        while (g_shortcuts[count].keys && count < 64) count++;   /* table is NULL-terminated */
        float row_h = 32.0f, cw = 480.0f, ch = 72.0f + (float)count * row_h + 40.0f;
        nxc_rect_t card = nxc_rect((float)(int32_t)((sw - cw) * 0.5f), (float)(int32_t)((sh - ch) * 0.5f), cw, ch);
        nxglass_panel(c, card, t->radius_sheet, NXGLASS_REGULAR, 4, 0);
        nxsym_draw(c, NXSYM_KEYBOARD, card.x + 38.0f, card.y + 36.0f, 24.0f, t->accent, 0.0f);
        nxui_text_in_rect(c, NXUI_TEXT_TITLE2, nxc_rect(card.x + 60.0f, card.y + 18.0f, cw - 84.0f, 36.0f),
                          "Keyboard shortcuts", t->label, NXC_ALIGN_LEFT);
        nxglass_separator(c, card.x + 20.0f, card.y + 62.0f, cw - 40.0f);
        for (int i = 0; i < count; i++) {
            float y = card.y + 70.0f + (float)i * row_h;
            float kw = nxui_text_width(NXUI_TEXT_BODY_MEDIUM, g_shortcuts[i].keys) + 18.0f;
            if (kw > 170.0f) kw = 170.0f;
            kbd_keycap(c, nxc_rect(card.x + 24.0f, y + 5.0f, kw, row_h - 11.0f), g_shortcuts[i].keys);
            nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(card.x + 210.0f, y, cw - 234.0f, row_h), g_shortcuts[i].action,
                              t->label_secondary, NXC_ALIGN_LEFT);
        }
        nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(card.x, card.y + ch - 30.0f, cw, 18.0f), "Release NX to close",
                          t->label_tertiary, NXC_ALIGN_CENTER);
    }
    /* Design review gallery (nxglass_gallery_toggle); no-op unless shown. */
    nxglass_gallery_render(c);
}
