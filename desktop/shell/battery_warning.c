/*
 * NeolyxOS Battery Warning Popup
 * 
 * Modal warning when battery falls below thresholds.
 * Shows at 20% (yellow), 10% (orange), 5% (red).
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/battery_warning.h"
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxsyscall.h"

/* Warning State */
static battery_warning_state_t g_bw = {0};
static int g_bw_initialized = 0;

/* Configuration */
#define BW_WIDTH        320
#define BW_HEIGHT       104
#define BW_DURATION     8000    /* 8 seconds */
#define BW_FADE_TIME    500     /* 0.5 second fade */
#define BW_RADIUS       16

/* Colors */
#define BW_BG_COLOR     0xE8181820
#define BW_TEXT_COLOR   0xFFFFFFFF
#define BW_YELLOW       0xFFF9E2AF  /* 20% warning */
#define BW_ORANGE       0xFFFFA500  /* 10% warning */
#define BW_RED          0xFFFF453A  /* 5% urgent */

/* Initialize */
void battery_warning_init(uint32_t screen_w, uint32_t screen_h) {
    g_bw.visible = 0;
    g_bw.battery_percent = 100;
    g_bw.is_charging = 0;
    g_bw.show_time = 0;
    g_bw.opacity = 255;
    
    /* Center on screen */
    g_bw.width = BW_WIDTH;
    g_bw.height = BW_HEIGHT;
    g_bw.x = (screen_w - BW_WIDTH) / 2;
    g_bw.y = (screen_h - BW_HEIGHT) / 2;
    
    g_bw.warned_20 = 0;
    g_bw.warned_10 = 0;
    g_bw.warned_5 = 0;
    
    g_bw_initialized = 1;
}

/* Show warning popup */
static void show_warning(int percent) {
    g_bw.battery_percent = percent;
    g_bw.visible = 1;
    g_bw.show_time = nx_gettime();
    g_bw.opacity = 255;
}

/* Check battery and trigger warnings at thresholds */
void battery_warning_check(int battery_percent, int is_charging) {
    if (!g_bw_initialized) return;
    
    g_bw.is_charging = is_charging;
    
    /* Reset warnings when charging */
    if (is_charging) {
        g_bw.warned_20 = 0;
        g_bw.warned_10 = 0;
        g_bw.warned_5 = 0;
        return;
    }
    
    /* Check thresholds */
    if (battery_percent <= BATTERY_URGENT_THRESHOLD && !g_bw.warned_5) {
        show_warning(battery_percent);
        g_bw.warned_5 = 1;
    } else if (battery_percent <= BATTERY_CRITICAL_THRESHOLD && !g_bw.warned_10) {
        show_warning(battery_percent);
        g_bw.warned_10 = 1;
    } else if (battery_percent <= BATTERY_LOW_THRESHOLD && !g_bw.warned_20) {
        show_warning(battery_percent);
        g_bw.warned_20 = 1;
    }
}

/* Dismiss popup */
void battery_warning_dismiss(void) {
    g_bw.visible = 0;
    g_bw.opacity = 0;
}

/* Check visibility */
int battery_warning_is_visible(void) {
    return g_bw.visible;
}

/* Reset warning flags */
void battery_warning_reset(void) {
    g_bw.warned_20 = 0;
    g_bw.warned_10 = 0;
    g_bw.warned_5 = 0;
}

/* Update fade animation */
void battery_warning_tick(void) {
    if (!g_bw.visible) return;
    
    uint64_t now = nx_gettime();
    uint64_t elapsed = now - g_bw.show_time;
    
    if (elapsed > BW_DURATION) {
        uint64_t fade_elapsed = elapsed - BW_DURATION;
        
        if (fade_elapsed >= BW_FADE_TIME) {
            g_bw.visible = 0;
            g_bw.opacity = 0;
        } else {
            g_bw.opacity = 255 - (uint8_t)((fade_elapsed * 255) / BW_FADE_TIME);
        }
    }
}

/* ============ Canvas renderer ============ */

/* Low battery alert: regular glass card, tinted symbol well, level bar. */
void battery_warning_render(nxc_canvas_t *c) {
    if (!c || !g_bw.visible || g_bw.opacity == 0) return;
    const nxglass_tokens_t *t = nxglass_tokens();
    nxc_rect_t box = nxc_rect((float)g_bw.x, (float)g_bw.y, (float)g_bw.width, (float)g_bw.height);
    int pct = g_bw.battery_percent < 0 ? 0 : (g_bw.battery_percent > 100 ? 100 : g_bw.battery_percent);
    float level = (float)pct / 100.0f;
    nxc_color_t col = pct <= BATTERY_CRITICAL_THRESHOLD ? t->destructive : t->warning;
    char line[48];
    int n = 0;
    {
        char tmp[8]; int k = 0, v = pct;
        if (v == 0) tmp[k++] = '0';
        while (v > 0 && k < 7) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
        while (k > 0) line[n++] = tmp[--k];
        const char *b = g_bw.is_charging ? "% \xC2\xB7 charging" : "% remaining \xC2\xB7 connect to power soon";
        while (*b && n < 46) line[n++] = *b++;
        line[n] = '\0';
    }
    nxc_save(c);
    nxc_set_alpha(c, g_bw.opacity);
    nxglass_panel(c, box, t->radius_sheet, NXGLASS_REGULAR, 3, 0);
    float cy = box.y + 40.0f;
    nxc_fill_circle(c, box.x + 40.0f, cy, 22.0f, NXC_WITH_ALPHA(col, 46));
    nxsym_draw(c, g_bw.is_charging ? NXSYM_BATTERY_CHARGING : NXSYM_BATTERY, box.x + 40.0f, cy, 26.0f, col, level);
    nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(box.x + 76.0f, cy - 19.0f, box.w - 92.0f, 20.0f),
                      pct <= BATTERY_URGENT_THRESHOLD ? "Battery critically low" : "Low battery", t->label, NXC_ALIGN_LEFT);
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(box.x + 76.0f, cy + 1.0f, box.w - 92.0f, 18.0f), line,
                      t->label_secondary, NXC_ALIGN_LEFT);
    nxc_rect_t bar = nxc_rect(box.x + 20.0f, box.y + box.h - 22.0f, box.w - 40.0f, 6.0f);
    nxc_fill_rrect(c, bar, 3.0f, t->fill_pressed);
    float fw = bar.w * level;
    if (fw < bar.h) fw = bar.h;
    nxc_paint_t p = nxc_paint_linear(bar.x, 0.0f, bar.x + bar.w, 0.0f, nxc_color_lerp(col, NXC_RGB(0xFF, 0xFF, 0xFF), 0.2f), col);
    nxc_fill_rrect_paint(c, nxc_rect(bar.x, bar.y, fw, bar.h), 3.0f, &p);
    nxc_restore(c);
}
