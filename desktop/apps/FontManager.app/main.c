/*
 * NeolyxOS Font Manager - Main Application
 * 
 * Modern font management with Redux-style UI and NXRender.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "state.h"
#include "ttf.h"

/* ============ External Dependencies ============ */

#ifdef __KERNEL__
/* Kernel mode - use kernel APIs */
extern void serial_puts(const char *s);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern uint32_t fb_width, fb_height, fb_pitch;
extern volatile uint32_t *framebuffer;
#else
/* Userspace mode - use syscalls */
#include <stdlib.h>
#include <stdio.h>
#define serial_puts(s) printf("%s", s)
#define kmalloc(s) malloc(s)
#define kfree(p) free(p)
#endif

/* ============ Global State (Redux Store) ============ */

static fm_state_t g_state;
static int g_initialized = 0;

/* ============ String Helpers ============ */

static int fm_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void fm_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int fm_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

/* ============ State Initialization ============ */

void fm_state_init(fm_state_t *state) {
    /* Zero initialize */
    for (uint32_t i = 0; i < sizeof(fm_state_t); i++) {
        ((uint8_t*)state)[i] = 0;
    }
    
    /* Defaults */
    state->selected_font_idx = -1;
    state->preview_size = 24;
    fm_strcpy(state->preview_text, "The quick brown fox jumps over the lazy dog", FM_MAX_PREVIEW_LEN);
    state->filter = FM_FILTER_ALL;
    state->sort = FM_SORT_NAME_ASC;
    state->sidebar_visible = 1;
    state->preview_panel_visible = 1;
    
    /* Default window size */
    state->window_x = 100;
    state->window_y = 60;
    state->window_w = 700;
    state->window_h = 500;
    
    serial_puts("[FONTMGR] State initialized\n");
}

/* ============ Reducer (Action Handler) ============ */

void fm_dispatch(fm_state_t *state, const fm_action_t *action) {
    if (!state || !action) return;
    
    switch (action->type) {
        case FM_ACTION_INIT:
            fm_state_init(state);
            break;
            
        case FM_ACTION_SELECT_FONT:
            if (action->payload.select.font_idx >= -1 && 
                action->payload.select.font_idx < (int32_t)state->font_count) {
                state->selected_font_idx = action->payload.select.font_idx;
            }
            break;
            
        case FM_ACTION_PREVIEW_SIZE:
            if (action->payload.preview_size.size >= 8 && 
                action->payload.preview_size.size <= 144) {
                state->preview_size = action->payload.preview_size.size;
            }
            break;
            
        case FM_ACTION_PREVIEW_TEXT:
            if (action->payload.preview_text.text) {
                fm_strcpy(state->preview_text, action->payload.preview_text.text, FM_MAX_PREVIEW_LEN);
            }
            break;
            
        case FM_ACTION_FILTER:
            state->filter = action->payload.filter.filter;
            break;
            
        case FM_ACTION_SORT:
            state->sort = action->payload.sort.sort;
            break;
            
        case FM_ACTION_ERROR:
            state->error_visible = 1;
            if (action->payload.error.message) {
                fm_strcpy(state->error_message, action->payload.error.message, 128);
            }
            break;
            
        default:
            break;
    }
}

const fm_state_t* fm_get_state(void) {
    return &g_state;
}

/* ============ Selectors ============ */

const fm_font_entry_t* fm_select_current_font(const fm_state_t *state) {
    if (!state || state->selected_font_idx < 0 || 
        state->selected_font_idx >= (int32_t)state->font_count) {
        return NULL;
    }
    return &state->fonts[state->selected_font_idx];
}

uint32_t fm_select_count_installed(const fm_state_t *state) {
    if (!state) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < state->font_count; i++) {
        if (state->fonts[i].installed) count++;
    }
    return count;
}

uint32_t fm_select_count_system(const fm_state_t *state) {
    if (!state) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < state->font_count; i++) {
        if (state->fonts[i].system_font) count++;
    }
    return count;
}

/* ============ Built-in Fonts ============ */

static void load_builtin_fonts(fm_state_t *state) {
    /* Add NeolyxOS system fonts */
    fm_font_entry_t *f;
    
    f = &state->fonts[state->font_count++];
    f->id = 1;
    fm_strcpy(f->name, "NeoLyx System", FM_MAX_NAME_LEN);
    fm_strcpy(f->family, "NeoLyx", FM_MAX_NAME_LEN);
    fm_strcpy(f->path, "/System/Fonts/NeoLyx-System.nxf", 128);
    f->type = FM_FONT_NXFONT;
    f->weight = FM_WEIGHT_REGULAR;
    f->style = FM_STYLE_NORMAL;
    f->installed = 1;
    f->system_font = 1;
    f->glyph_count = 256;
    
    f = &state->fonts[state->font_count++];
    f->id = 2;
    fm_strcpy(f->name, "NeoLyx Mono", FM_MAX_NAME_LEN);
    fm_strcpy(f->family, "NeoLyx", FM_MAX_NAME_LEN);
    fm_strcpy(f->path, "/System/Fonts/NeoLyx-Mono.nxf", 128);
    f->type = FM_FONT_NXFONT;
    f->weight = FM_WEIGHT_REGULAR;
    f->style = FM_STYLE_NORMAL;
    f->installed = 1;
    f->system_font = 1;
    f->glyph_count = 256;
    
    f = &state->fonts[state->font_count++];
    f->id = 3;
    fm_strcpy(f->name, "NeoLyx Bold", FM_MAX_NAME_LEN);
    fm_strcpy(f->family, "NeoLyx", FM_MAX_NAME_LEN);
    fm_strcpy(f->path, "/System/Fonts/NeoLyx-Bold.nxf", 128);
    f->type = FM_FONT_NXFONT;
    f->weight = FM_WEIGHT_BOLD;
    f->style = FM_STYLE_NORMAL;
    f->installed = 1;
    f->system_font = 1;
    f->glyph_count = 256;
    
    /* Add sample open fonts */
    f = &state->fonts[state->font_count++];
    f->id = 4;
    fm_strcpy(f->name, "Open Sans", FM_MAX_NAME_LEN);
    fm_strcpy(f->family, "Open Sans", FM_MAX_NAME_LEN);
    fm_strcpy(f->path, "/Library/Fonts/OpenSans.ttf", 128);
    f->type = FM_FONT_TTF;
    f->weight = FM_WEIGHT_REGULAR;
    f->style = FM_STYLE_NORMAL;
    f->installed = 0;
    f->system_font = 0;
    f->glyph_count = 897;
    
    f = &state->fonts[state->font_count++];
    f->id = 5;
    fm_strcpy(f->name, "Roboto", FM_MAX_NAME_LEN);
    fm_strcpy(f->family, "Roboto", FM_MAX_NAME_LEN);
    fm_strcpy(f->path, "/Library/Fonts/Roboto.ttf", 128);
    f->type = FM_FONT_TTF;
    f->weight = FM_WEIGHT_REGULAR;
    f->style = FM_STYLE_NORMAL;
    f->installed = 0;
    f->system_font = 0;
    f->glyph_count = 1294;
    
    f = &state->fonts[state->font_count++];
    f->id = 6;
    fm_strcpy(f->name, "Fira Code", FM_MAX_NAME_LEN);
    fm_strcpy(f->family, "Fira Code", FM_MAX_NAME_LEN);
    fm_strcpy(f->path, "/Library/Fonts/FiraCode.ttf", 128);
    f->type = FM_FONT_TTF;
    f->weight = FM_WEIGHT_REGULAR;
    f->style = FM_STYLE_NORMAL;
    f->installed = 0;
    f->system_font = 0;
    f->glyph_count = 1485;
    
    serial_puts("[FONTMGR] Loaded built-in fonts\n");
}

/* ============ NXRender Integration ============ */

/* ============ Glassmorphism Colors (ARGB with alpha) ============ */

/* Base colors - catppuccin inspired with glassmorphism */
#define FM_BG_COLOR        0xFF11111B  /* Deep dark background */
#define FM_BG_BLUR         0xD01E1E2E  /* Blurred glass overlay (80% opacity) */

/* Glass panels - semi-transparent */
#define FM_GLASS_DARK      0xC0181825  /* Dark glass (75% opacity) */
#define FM_GLASS_MEDIUM    0xB02D2D3D  /* Medium glass (70% opacity) */
#define FM_GLASS_LIGHT     0xA03D3D4D  /* Light glass (65% opacity) */
#define FM_GLASS_ACCENT    0x904D7FFF  /* Accent glass (55% opacity) */

/* Solid colors for contrast */
#define FM_SIDEBAR_BG      0xE8181825  /* Sidebar - mostly opaque */
#define FM_HEADER_BG       0xD02D2D3D  /* Header - glass effect */
#define FM_SELECTED_BG     0xE04D7FFF  /* Selected item - vibrant */
#define FM_HOVER_BG        0x803D3D6D  /* Hover - subtle highlight */

/* Text - high contrast for readability */
#define FM_TEXT_PRIMARY    0xFFFFFFFF  /* Pure white - maximum contrast */
#define FM_TEXT_SECONDARY  0xFFBAC2DE  /* Lavender gray - softer */
#define FM_TEXT_DIM        0xFF6C7086  /* Dimmed - for less important */
#define FM_TEXT_ACCENT     0xFF89B4FA  /* Blue accent - links/actions */
#define FM_TEXT_HEADING    0xFFF5E0DC  /* Warm white - headings */

/* UI elements */
#define FM_BORDER_COLOR    0x6045475A  /* Border - subtle (40% opacity) */
#define FM_BORDER_ACCENT   0x8089B4FA  /* Accent border (50% opacity) */
#define FM_BUTTON_BG       0xF089B4FA  /* Button - vibrant blue */
#define FM_BUTTON_HOVER    0xFFACC7FF  /* Button hover - lighter */
#define FM_BUTTON_PRESSED  0xE07095E0  /* Button pressed - darker */

/* Status indicators */
#define FM_STATUS_SUCCESS  0xFFA6E3A1  /* Green - installed */
#define FM_STATUS_WARNING  0xFFF9E2AF  /* Yellow - warning */
#define FM_STATUS_ERROR    0xFFF38BA8  /* Red - error */

/* Drawing primitives (using framebuffer directly) */
#ifdef __KERNEL__

/* Alpha blend helper for glassmorphism */
static inline uint32_t fm_blend_alpha(uint32_t fg, uint32_t bg) {
    uint8_t fg_a = (fg >> 24) & 0xFF;
    if (fg_a == 0xFF) return fg;  /* Fully opaque */
    if (fg_a == 0x00) return bg;  /* Fully transparent */
    
    uint8_t fg_r = (fg >> 16) & 0xFF;
    uint8_t fg_g = (fg >> 8) & 0xFF;
    uint8_t fg_b = fg & 0xFF;
    
    uint8_t bg_r = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bg_b = bg & 0xFF;
    
    /* Alpha blend: out = fg * a + bg * (1-a) */
    uint8_t out_r = (fg_r * fg_a + bg_r * (255 - fg_a)) / 255;
    uint8_t out_g = (fg_g * fg_a + bg_g * (255 - fg_a)) / 255;
    uint8_t out_b = (fg_b * fg_a + bg_b * (255 - fg_a)) / 255;
    
    return 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
}

/* Fill rectangle with alpha blending for glass effect */
static void fm_fill_rect(int x, int y, int w, int h, uint32_t color) {
    uint8_t alpha = (color >> 24) & 0xFF;
    int do_blend = (alpha < 0xFF);
    
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int px = x + i;
            int py = y + j;
            if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                uint32_t idx = py * (fb_pitch / 4) + px;
                if (do_blend) {
                    framebuffer[idx] = fm_blend_alpha(color, framebuffer[idx]);
                } else {
                    framebuffer[idx] = color;
                }
            }
        }
    }
}

/* Draw rounded rectangle border for glass cards */
static void fm_draw_rect(int x, int y, int w, int h, uint32_t color) {
    uint8_t alpha = (color >> 24) & 0xFF;
    int do_blend = (alpha < 0xFF);
    
    /* Top and bottom */
    for (int i = 0; i < w; i++) {
        if (x + i >= 0 && x + i < (int)fb_width) {
            if (y >= 0 && y < (int)fb_height) {
                uint32_t idx = y * (fb_pitch / 4) + x + i;
                framebuffer[idx] = do_blend ? fm_blend_alpha(color, framebuffer[idx]) : color;
            }
            if (y + h - 1 >= 0 && y + h - 1 < (int)fb_height) {
                uint32_t idx = (y + h - 1) * (fb_pitch / 4) + x + i;
                framebuffer[idx] = do_blend ? fm_blend_alpha(color, framebuffer[idx]) : color;
            }
        }
    }
    /* Left and right */
    for (int j = 0; j < h; j++) {
        if (y + j >= 0 && y + j < (int)fb_height) {
            if (x >= 0 && x < (int)fb_width) {
                uint32_t idx = (y + j) * (fb_pitch / 4) + x;
                framebuffer[idx] = do_blend ? fm_blend_alpha(color, framebuffer[idx]) : color;
            }
            if (x + w - 1 >= 0 && x + w - 1 < (int)fb_width) {
                uint32_t idx = (y + j) * (fb_pitch / 4) + x + w - 1;
                framebuffer[idx] = do_blend ? fm_blend_alpha(color, framebuffer[idx]) : color;
            }
        }
    }
}

/* Draw glass card with border glow */
static void fm_draw_glass_card(int x, int y, int w, int h, uint32_t bg, uint32_t border) {
    /* Glass background */
    fm_fill_rect(x, y, w, h, bg);
    /* Subtle top highlight for depth */
    fm_fill_rect(x, y, w, 1, 0x20FFFFFF);
    /* Border */
    fm_draw_rect(x, y, w, h, border);
}

/* Simple 8x8 bitmap font for UI */
extern void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);
#define fm_draw_text desktop_draw_text
#else
#define fm_fill_rect(x,y,w,h,c)
#define fm_draw_rect(x,y,w,h,c)
#define fm_draw_glass_card(x,y,w,h,b,c)
#define fm_draw_text(x,y,t,c)
#endif

/* ============ View Mode ============ */

typedef enum {
    FM_VIEW_LIST,       /* Simple list */
    FM_VIEW_GROUPED     /* Grouped by family */
} fm_view_mode_t;

static fm_view_mode_t g_view_mode = FM_VIEW_GROUPED;
static int g_drag_active = 0;
static int g_drag_x = 0, g_drag_y = 0;

/* ============ Toolbar ============ */

static void render_toolbar(const fm_state_t *state) {
    int x = state->window_x + 180;  /* After sidebar */
    int y = state->window_y + 28;
    int w = state->window_w - 180;
    int h = 40;
    
    /* Glass toolbar background */
    fm_draw_glass_card(x, y, w, h, FM_GLASS_MEDIUM, FM_BORDER_COLOR);
    
    /* Add Font button - vibrant */
    int bx = x + 12;
    int by = y + 8;
    fm_fill_rect(bx, by, 90, 24, FM_BUTTON_BG);
    fm_fill_rect(bx, by, 90, 1, 0x40FFFFFF);  /* Highlight */
    fm_draw_text(bx + 10, by + 7, "+ Add Font", FM_TEXT_PRIMARY);
    
    /* Remove button - secondary style */
    bx += 100;
    fm_fill_rect(bx, by, 80, 24, FM_GLASS_DARK);
    fm_draw_rect(bx, by, 80, 24, FM_BORDER_COLOR);
    fm_draw_text(bx + 14, by + 7, "Remove", FM_TEXT_SECONDARY);
    
    /* Spacer */
    
    /* View mode toggle - right aligned */
    bx = x + w - 180;
    fm_draw_text(bx, by + 7, "View:", FM_TEXT_DIM);
    bx += 45;
    
    /* List button */
    uint32_t list_bg = g_view_mode == FM_VIEW_LIST ? FM_SELECTED_BG : FM_GLASS_DARK;
    fm_fill_rect(bx, by, 55, 24, list_bg);
    fm_draw_rect(bx, by, 55, 24, FM_BORDER_COLOR);
    fm_draw_text(bx + 12, by + 7, "List", 
                 g_view_mode == FM_VIEW_LIST ? FM_TEXT_PRIMARY : FM_TEXT_SECONDARY);
    
    /* Grouped button */
    bx += 60;
    uint32_t fam_bg = g_view_mode == FM_VIEW_GROUPED ? FM_SELECTED_BG : FM_GLASS_DARK;
    fm_fill_rect(bx, by, 65, 24, fam_bg);
    fm_draw_rect(bx, by, 65, 24, FM_BORDER_COLOR);
    fm_draw_text(bx + 8, by + 7, "Family",
                 g_view_mode == FM_VIEW_GROUPED ? FM_TEXT_PRIMARY : FM_TEXT_SECONDARY);
}

/* ============ UI Components ============ */

static void render_sidebar(const fm_state_t *state) {
    int x = state->window_x;
    int y = state->window_y + 28;  /* Below title bar */
    int w = 180;
    int h = state->window_h - 28;
    
    /* Glass sidebar background */
    fm_draw_glass_card(x, y, w, h, FM_GLASS_DARK, FM_BORDER_COLOR);
    
    /* Filter categories */
    int ty = y + 20;
    fm_draw_text(x + 14, ty, "FONTS", FM_TEXT_DIM);
    ty += 24;
    
    const char *filters[] = {"All Fonts", "Installed", "System", "User"};
    fm_filter_t filter_vals[] = {FM_FILTER_ALL, FM_FILTER_INSTALLED, FM_FILTER_SYSTEM, FM_FILTER_USER};
    
    for (int i = 0; i < 4; i++) {
        if (state->filter == filter_vals[i]) {
            /* Selected item - glass accent */
            fm_fill_rect(x + 6, ty - 4, w - 12, 22, FM_GLASS_ACCENT);
            fm_draw_rect(x + 6, ty - 4, w - 12, 22, FM_BORDER_ACCENT);
            fm_draw_text(x + 18, ty, filters[i], FM_TEXT_PRIMARY);
        } else {
            /* Normal item - subtle hover area */
            fm_draw_text(x + 18, ty, filters[i], FM_TEXT_SECONDARY);
        }
        ty += 26;
    }
    
    /* Divider */
    ty += 8;
    fm_fill_rect(x + 12, ty, w - 24, 1, FM_BORDER_COLOR);
    ty += 16;
    
    /* Collections section */
    fm_draw_text(x + 14, ty, "COLLECTIONS", FM_TEXT_DIM);
    ty += 24;
    
    fm_draw_text(x + 18, ty, "Serif", FM_TEXT_SECONDARY);
    ty += 24;
    fm_draw_text(x + 18, ty, "Sans-Serif", FM_TEXT_SECONDARY);
    ty += 24;
    fm_draw_text(x + 18, ty, "Monospace", FM_TEXT_SECONDARY);
}

static void render_font_list(const fm_state_t *state) {
    int x = state->window_x + 180;  /* After sidebar */
    int y = state->window_y + 28 + 36;  /* Below toolbar */
    int w = state->window_w - 180 - 250;  /* Middle panel */
    int h = state->window_h - 28 - 36;
    
    /* List background */
    fm_fill_rect(x, y, w, h, FM_BG_COLOR);
    fm_draw_rect(x + w - 1, y, 1, h, FM_BORDER_COLOR);
    
    /* Drag and drop indicator */
    if (g_drag_active) {
        fm_fill_rect(x + 10, y + h/2 - 30, w - 20, 60, 0xFF4D4D7D);
        fm_draw_rect(x + 10, y + h/2 - 30, w - 20, 60, FM_BUTTON_BG);
        fm_draw_text(x + w/2 - 60, y + h/2 - 8, "Drop font here to install", FM_TEXT_PRIMARY);
        return;
    }
    
    int ty = y + 8;
    
    if (g_view_mode == FM_VIEW_GROUPED) {
        /* ===== GROUPED BY FAMILY VIEW ===== */
        
        /* Track which families we've rendered */
        char rendered_families[FM_MAX_FONTS][FM_MAX_NAME_LEN];
        int rendered_count = 0;
        
        for (uint32_t i = 0; i < state->font_count && ty < y + h - 40; i++) {
            const fm_font_entry_t *f = &state->fonts[i];
            
            /* Check if we already rendered this family */
            int already_rendered = 0;
            for (int r = 0; r < rendered_count; r++) {
                if (fm_strcmp(rendered_families[r], f->family) == 0) {
                    already_rendered = 1;
                    break;
                }
            }
            if (already_rendered) continue;
            
            /* Mark as rendered */
            fm_strcpy(rendered_families[rendered_count++], f->family, FM_MAX_NAME_LEN);
            
            /* Count variants in this family */
            int variant_count = 0;
            int installed_count = 0;
            for (uint32_t j = 0; j < state->font_count; j++) {
                if (fm_strcmp(state->fonts[j].family, f->family) == 0) {
                    variant_count++;
                    if (state->fonts[j].installed) installed_count++;
                }
            }
            
            /* Family header */
            int fam_selected = 0;
            if (state->selected_font_idx >= 0 && 
                fm_strcmp(state->fonts[state->selected_font_idx].family, f->family) == 0) {
                fam_selected = 1;
                fm_fill_rect(x, ty, w, 28, FM_SELECTED_BG);
            }
            
            /* Family name */
            fm_draw_text(x + 12, ty + 6, f->family, FM_TEXT_PRIMARY);
            
            /* Variant count */
            char count_str[16];
            int idx = 0;
            if (variant_count >= 10) count_str[idx++] = '0' + (variant_count / 10) % 10;
            count_str[idx++] = '0' + variant_count % 10;
            count_str[idx++] = ' ';
            count_str[idx++] = 's'; count_str[idx++] = 't'; count_str[idx++] = 'y';
            count_str[idx++] = 'l'; count_str[idx++] = 'e'; count_str[idx++] = 's';
            count_str[idx] = '\0';
            fm_draw_text(x + w - 80, ty + 6, count_str, FM_TEXT_SECONDARY);
            
            ty += 28;
            
            /* Show variants if family is selected */
            if (fam_selected) {
                for (uint32_t j = 0; j < state->font_count && ty < y + h - 24; j++) {
                    const fm_font_entry_t *v = &state->fonts[j];
                    if (fm_strcmp(v->family, f->family) != 0) continue;
                    
                    /* Indent for variant */
                    if ((int32_t)j == state->selected_font_idx) {
                        fm_fill_rect(x + 20, ty, w - 20, 22, 0xFF5D8FFF);
                    }
                    
                    /* Style name */
                    const char *weight_names[] = {"Thin", "Light", "Regular", "Medium", "Bold", "Black"};
                    int weight_idx = (v->weight / 200);
                    if (weight_idx > 5) weight_idx = 5;
                    fm_draw_text(x + 32, ty + 4, weight_names[weight_idx], FM_TEXT_PRIMARY);
                    
                    /* Italic indicator */
                    if (v->style == FM_STYLE_ITALIC) {
                        fm_draw_text(x + 100, ty + 4, "Italic", FM_TEXT_SECONDARY);
                    }
                    
                    /* Installed status */
                    if (v->installed) {
                        fm_draw_text(x + w - 70, ty + 4, "Active", FM_TEXT_ACCENT);
                    }
                    
                    ty += 22;
                }
            }
        }
    } else {
        /* ===== SIMPLE LIST VIEW ===== */
        
        /* Header */
        fm_fill_rect(x, y, w, 28, FM_HEADER_BG);
        fm_draw_text(x + 12, y + 8, "Font Name", FM_TEXT_SECONDARY);
        fm_draw_text(x + w - 100, y + 8, "Status", FM_TEXT_SECONDARY);
        ty = y + 32;
        
        int item_h = 36;
        
        for (uint32_t i = 0; i < state->font_count && ty < y + h - item_h; i++) {
            const fm_font_entry_t *f = &state->fonts[i];
            
            /* Selection highlight */
            if ((int32_t)i == state->selected_font_idx) {
                fm_fill_rect(x, ty, w, item_h, FM_SELECTED_BG);
            }
            
            /* Font name */
            fm_draw_text(x + 12, ty + 6, f->name, FM_TEXT_PRIMARY);
            
            /* Font family (smaller) */
            fm_draw_text(x + 12, ty + 20, f->family, FM_TEXT_SECONDARY);
            
            /* Status */
            if (f->installed) {
                fm_draw_text(x + w - 80, ty + 12, "Installed", FM_TEXT_ACCENT);
            }
            
            ty += item_h;
        }
    }
}

static void render_preview_panel(const fm_state_t *state) {
    int x = state->window_x + state->window_w - 250;
    int y = state->window_y + 28;
    int w = 250;
    int h = state->window_h - 28;
    
    /* Preview background */
    fm_fill_rect(x, y, w, h, FM_SIDEBAR_BG);
    
    const fm_font_entry_t *font = fm_select_current_font(state);
    
    if (!font) {
        fm_draw_text(x + 20, y + h/2 - 8, "Select a font", FM_TEXT_SECONDARY);
        return;
    }
    
    /* Font info */
    int ty = y + 16;
    fm_draw_text(x + 12, ty, font->name, FM_TEXT_PRIMARY);
    ty += 24;
    
    fm_draw_text(x + 12, ty, "Family:", FM_TEXT_SECONDARY);
    fm_draw_text(x + 80, ty, font->family, FM_TEXT_PRIMARY);
    ty += 18;
    
    fm_draw_text(x + 12, ty, "Type:", FM_TEXT_SECONDARY);
    const char *type_names[] = {"TTF", "OTF", "WOFF", "NXF"};
    fm_draw_text(x + 80, ty, type_names[font->type], FM_TEXT_PRIMARY);
    ty += 18;
    
    fm_draw_text(x + 12, ty, "Weight:", FM_TEXT_SECONDARY);
    char weight_str[8] = {'0' + (font->weight / 100), '0', '0', '\0'};
    fm_draw_text(x + 80, ty, weight_str, FM_TEXT_PRIMARY);
    ty += 18;
    
    fm_draw_text(x + 12, ty, "Glyphs:", FM_TEXT_SECONDARY);
    char glyph_str[16];
    int gc = font->glyph_count;
    int idx = 0;
    if (gc >= 1000) glyph_str[idx++] = '0' + (gc / 1000) % 10;
    if (gc >= 100) glyph_str[idx++] = '0' + (gc / 100) % 10;
    if (gc >= 10) glyph_str[idx++] = '0' + (gc / 10) % 10;
    glyph_str[idx++] = '0' + gc % 10;
    glyph_str[idx] = '\0';
    fm_draw_text(x + 80, ty, glyph_str, FM_TEXT_PRIMARY);
    ty += 30;
    
    /* Preview text */
    fm_draw_text(x + 12, ty, "PREVIEW", FM_TEXT_SECONDARY);
    ty += 20;
    
    /* Preview area */
    fm_fill_rect(x + 8, ty, w - 16, 120, FM_BG_COLOR);
    fm_draw_rect(x + 8, ty, w - 16, 120, FM_BORDER_COLOR);
    
    fm_draw_text(x + 16, ty + 10, "AaBbCc", FM_TEXT_PRIMARY);
    fm_draw_text(x + 16, ty + 30, "123456", FM_TEXT_PRIMARY);
    fm_draw_text(x + 16, ty + 50, "!@#$%^", FM_TEXT_PRIMARY);
    fm_draw_text(x + 16, ty + 80, state->preview_text, FM_TEXT_SECONDARY);
    
    ty += 140;
    
    /* Install/Uninstall button */
    if (!font->system_font) {
        fm_fill_rect(x + 12, ty, w - 24, 32, font->installed ? FM_BORDER_COLOR : FM_BUTTON_BG);
        fm_draw_text(x + 60, ty + 10, font->installed ? "Uninstall" : "Install", FM_TEXT_PRIMARY);
    }
}

/* ============ Main Render Function ============ */

void fontmanager_render(void) {
    if (!g_initialized) return;
    
    /* Render all UI components */
    render_toolbar(&g_state);
    render_sidebar(&g_state);
    render_font_list(&g_state);
    render_preview_panel(&g_state);
}

/* ============ Event Handlers ============ */

/* Handle file drop for font installation */
void fontmanager_drop(const char *path) {
    serial_puts("[FONTMGR] File dropped: ");
    serial_puts(path);
    serial_puts("\n");
    
    /* Check if valid font file */
    int len = fm_strlen(path);
    if (len < 4) return;
    
    const char *ext = &path[len - 4];
    if ((ext[1] == 't' || ext[1] == 'T') &&
        (ext[2] == 't' || ext[2] == 'T') &&
        (ext[3] == 'f' || ext[3] == 'F')) {
        /* TTF file - install it */
        serial_puts("[FONTMGR] Installing TTF font\n");
        /* TODO: Call font_import_file(path) */
    }
    else if ((ext[1] == 'o' || ext[1] == 'O') &&
             (ext[2] == 't' || ext[2] == 'T') &&
             (ext[3] == 'f' || ext[3] == 'F')) {
        /* OTF file */
        serial_puts("[FONTMGR] Installing OTF font\n");
    }
    else if ((ext[1] == 'n' || ext[1] == 'N') &&
             (ext[2] == 'x' || ext[2] == 'X') &&
             (ext[3] == 'f' || ext[3] == 'F')) {
        /* NXF file (NeolyxOS font) */
        serial_puts("[FONTMGR] Installing NXF font\n");
    }
    
    g_drag_active = 0;
}

/* Handle drag enter/leave */
void fontmanager_drag_enter(int x, int y) {
    (void)x; (void)y;
    g_drag_active = 1;
    g_drag_x = x;
    g_drag_y = y;
}

void fontmanager_drag_leave(void) {
    g_drag_active = 0;
}

void fontmanager_click(int x, int y) {
    /* Check toolbar clicks first */
    int toolbar_x = g_state.window_x + 180;
    int toolbar_y = g_state.window_y + 28;
    int toolbar_w = g_state.window_w - 180;
    
    if (y >= toolbar_y && y < toolbar_y + 36) {
        /* Add Font button (x + 8, width 80) */
        if (x >= toolbar_x + 8 && x < toolbar_x + 88) {
            serial_puts("[FONTMGR] Add Font clicked - open file picker\n");
            /* TODO: Open file picker dialog */
            return;
        }
        
        /* Remove button (x + 98, width 70) */
        if (x >= toolbar_x + 98 && x < toolbar_x + 168) {
            if (g_state.selected_font_idx >= 0) {
                const fm_font_entry_t *f = &g_state.fonts[g_state.selected_font_idx];
                if (!f->system_font && f->installed) {
                    serial_puts("[FONTMGR] Remove font: ");
                    serial_puts(f->name);
                    serial_puts("\n");
                    /* TODO: Call font_remove(f->id) */
                }
            }
            return;
        }
        
        /* List view button (x + w - 115, width 50) */
        int list_btn_x = toolbar_x + toolbar_w - 115;
        if (x >= list_btn_x && x < list_btn_x + 50) {
            g_view_mode = FM_VIEW_LIST;
            serial_puts("[FONTMGR] View mode: List\n");
            return;
        }
        
        /* Family view button (x + w - 60, width 60) */
        int fam_btn_x = toolbar_x + toolbar_w - 60;
        if (x >= fam_btn_x && x < fam_btn_x + 60) {
            g_view_mode = FM_VIEW_GROUPED;
            serial_puts("[FONTMGR] View mode: Family\n");
            return;
        }
    }
    
    /* Check sidebar clicks */
    int sidebar_x = g_state.window_x;
    int sidebar_y = g_state.window_y + 28 + 36;
    
    if (x >= sidebar_x && x < sidebar_x + 180) {
        /* Filter selection */
        int filter_idx = (y - sidebar_y) / 22;
        if (filter_idx >= 0 && filter_idx < 4) {
            fm_action_t action = {.type = FM_ACTION_FILTER};
            fm_filter_t filters[] = {FM_FILTER_ALL, FM_FILTER_INSTALLED, FM_FILTER_SYSTEM, FM_FILTER_USER};
            action.payload.filter.filter = filters[filter_idx];
            fm_dispatch(&g_state, &action);
        }
    }
    
    /* Check font list clicks */
    int list_x = g_state.window_x + 180;
    int list_y = g_state.window_y + 28 + 36 + 8;  /* Below toolbar */
    int list_w = g_state.window_w - 180 - 250;
    
    if (x >= list_x && x < list_x + list_w && y >= list_y) {
        if (g_view_mode == FM_VIEW_GROUPED) {
            /* TODO: Handle grouped view clicks (expand/collapse families) */
            int row = (y - list_y) / 28;
            if (row >= 0 && row < (int)g_state.font_count) {
                fm_action_t action = {.type = FM_ACTION_SELECT_FONT};
                action.payload.select.font_idx = row;
                fm_dispatch(&g_state, &action);
            }
        } else {
            int font_idx = (y - list_y) / 36;
            if (font_idx >= 0 && font_idx < (int)g_state.font_count) {
                fm_action_t action = {.type = FM_ACTION_SELECT_FONT};
                action.payload.select.font_idx = font_idx;
                fm_dispatch(&g_state, &action);
            }
        }
    }
}

/* ============ Initialization ============ */

int fontmanager_init(int x, int y, int w, int h) {
    serial_puts("[FONTMGR] Initializing Font Manager...\n");
    
    fm_state_init(&g_state);
    g_state.window_x = x;
    g_state.window_y = y;
    g_state.window_w = w;
    g_state.window_h = h;
    
    load_builtin_fonts(&g_state);
    
    g_initialized = 1;
    serial_puts("[FONTMGR] Font Manager ready\n");
    
    return 0;
}

void fontmanager_shutdown(void) {
    g_initialized = 0;
    serial_puts("[FONTMGR] Font Manager shutdown\n");
}
