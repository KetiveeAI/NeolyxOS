/*
 * Reolab - IDE UI Renderer
 * NeolyxOS Application
 * 
 * Renders the IDE interface using the graphics backend.
 * This is the C implementation of the REOX UI layout.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "ui.h"
#include "../graphics/graphics.h"
#include <string.h>
#include <stdio.h>

/* UI Constants */
#define SIDEBAR_WIDTH   250
#define TOOLBAR_HEIGHT   40
#define STATUSBAR_HEIGHT 24
#define TAB_HEIGHT       35
#define LINE_HEIGHT      20
#define GUTTER_WIDTH     50

/* Draw the main IDE window */
void rl_ui_render(RLGraphics* gfx, ReoLabApp* app) {
    int w, h;
    /* Get window size */
    w = 1280; h = 800; /* TODO: get from gfx */
    
    /* Clear background */
    rl_graphics_clear(gfx, RL_BG_DARK);
    
    /* Draw sidebar */
    rl_ui_render_sidebar(gfx, app, 0, 0, SIDEBAR_WIDTH, h);
    
    /* Draw toolbar */
    rl_ui_render_toolbar(gfx, app, SIDEBAR_WIDTH, 0, w - SIDEBAR_WIDTH, TOOLBAR_HEIGHT);
    
    /* Draw tab bar */
    rl_ui_render_tabs(gfx, app, SIDEBAR_WIDTH, TOOLBAR_HEIGHT, w - SIDEBAR_WIDTH, TAB_HEIGHT);
    
    /* Draw editor area */
    float editor_y = TOOLBAR_HEIGHT + TAB_HEIGHT;
    float editor_h = h - editor_y - STATUSBAR_HEIGHT;
    rl_ui_render_editor(gfx, app, SIDEBAR_WIDTH, editor_y, w - SIDEBAR_WIDTH, editor_h);
    
    /* Draw status bar */
    rl_ui_render_statusbar(gfx, app, 0, h - STATUSBAR_HEIGHT, w, STATUSBAR_HEIGHT);
}

/* Render sidebar */
void rl_ui_render_sidebar(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h) {
    (void)app;
    
    /* Background */
    rl_graphics_fill_rect(gfx, (RLRect){x, y, w, h}, RL_BG_SIDEBAR);
    
    /* Header */
    rl_graphics_draw_text(gfx, "EXPLORER", x + 12, y + 12, 11, RL_TEXT_DIM);
    
    /* Project name */
    rl_graphics_draw_text(gfx, "reolab.app", x + 12, y + 36, 14, RL_TEXT);
    
    /* File tree placeholder */
    const char* files[] = {"main.c", "manifest.npa", "Makefile", "src/", "  app/", "    app.c", "  editor/", "    buffer.c", "ui/", "  main_window.reox"};
    for (int i = 0; i < 10; i++) {
        float ty = y + 60 + i * 22;
        rl_graphics_draw_text(gfx, files[i], x + 12, ty, 13, RL_TEXT);
    }
    
    /* Separator */
    rl_graphics_draw_line(gfx, x + w - 1, y, x + w - 1, y + h, RL_HEX(0x3C3C3C));
}

/* Render toolbar */
void rl_ui_render_toolbar(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h) {
    (void)app;
    
    /* Background */
    rl_graphics_fill_rect(gfx, (RLRect){x, y, w, h}, RL_BG_DARKER);
    
    /* Tool buttons */
    const char* tools[] = {"Open", "Save", "|", "Build", "Run"};
    float tx = x + 8;
    for (int i = 0; i < 5; i++) {
        if (tools[i][0] == '|') {
            /* Divider */
            rl_graphics_draw_line(gfx, tx + 4, y + 8, tx + 4, y + h - 8, RL_TEXT_DIM);
            tx += 16;
        } else {
            /* Button */
            float bw = 50;
            bool is_run = (i == 4);
            RLColor bg = is_run ? RL_ACCENT : RL_HEX(0x3C3C3C);
            rl_graphics_fill_rounded_rect(gfx, (RLRect){tx, y + 6, bw, h - 12}, bg, 4);
            rl_graphics_draw_text(gfx, tools[i], tx + 8, y + 12, 12, RL_TEXT);
            tx += bw + 8;
        }
    }
    
    /* Separator */
    rl_graphics_draw_line(gfx, x, y + h - 1, x + w, y + h - 1, RL_HEX(0x3C3C3C));
}

/* Render tab bar */
void rl_ui_render_tabs(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h) {
    /* Background */
    rl_graphics_fill_rect(gfx, (RLRect){x, y, w, h}, RL_BG_DARKER);
    
    /* Tabs */
    float tx = x;
    for (int i = 0; i < app->tab_count && i < 10; i++) {
        bool active = (i == app->active_tab);
        float tw = 120;
        
        /* Tab background */
        RLColor bg = active ? RL_BG_DARK : RL_BG_DARKER;
        rl_graphics_fill_rect(gfx, (RLRect){tx, y, tw, h}, bg);
        
        /* Tab text */
        const char* name = strrchr(app->tabs[i].path, '/');
        name = name ? name + 1 : app->tabs[i].path;
        rl_graphics_draw_text(gfx, name, tx + 12, y + 10, 13, active ? RL_TEXT : RL_TEXT_DIM);
        
        /* Modified indicator */
        if (app->tabs[i].modified) {
            rl_graphics_fill_rect(gfx, (RLRect){tx + tw - 16, y + 14, 6, 6}, RL_TEXT);
        }
        
        tx += tw;
    }
    
    /* Separator */
    rl_graphics_draw_line(gfx, x, y + h - 1, x + w, y + h - 1, RL_HEX(0x3C3C3C));
}

/* Render editor area */
void rl_ui_render_editor(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h) {
    /* Background */
    rl_graphics_fill_rect(gfx, (RLRect){x, y, w, h}, RL_BG_DARK);
    
    /* Line number gutter */
    rl_graphics_fill_rect(gfx, (RLRect){x, y, GUTTER_WIDTH, h}, RL_HEX(0x1E1E1E));
    
    if (app->active_tab < 0 || !app->tabs[app->active_tab].buffer) {
        /* No file open */
        rl_graphics_draw_text(gfx, "Open a file to start editing", x + w/2 - 100, y + h/2, 14, RL_TEXT_DIM);
        return;
    }
    
    ReoLabBuffer* buf = app->tabs[app->active_tab].buffer;
    const char* text = reolab_buffer_get_text(buf);
    
    /* Render lines */
    float ly = y + 4;
    int line_num = 1;
    const char* line_start = text;
    
    for (const char* p = text; ; p++) {
        if (*p == '\n' || *p == '\0') {
            /* Draw line number */
            char num_str[16];
            snprintf(num_str, sizeof(num_str), "%4d", line_num);
            rl_graphics_draw_text(gfx, num_str, x + 4, ly, 13, RL_TEXT_DIM);
            
            /* Draw line content */
            size_t len = p - line_start;
            if (len > 0) {
                char line_buf[512];
                size_t copy_len = len < 511 ? len : 511;
                memcpy(line_buf, line_start, copy_len);
                line_buf[copy_len] = '\0';
                rl_graphics_draw_text(gfx, line_buf, x + GUTTER_WIDTH + 8, ly, 13, RL_TEXT);
            }
            
            ly += LINE_HEIGHT;
            line_num++;
            line_start = p + 1;
            
            if (*p == '\0' || ly > y + h) break;
        }
    }
    
    /* Draw cursor (| bar style) */
    {
        /* Calculate cursor position - for now, at line 1 col 1 */
        float cursor_line = 0;
        float cursor_col = 0;
        float char_width = 8.2f;  /* Approximate monospace char width */
        
        float cursor_x = x + GUTTER_WIDTH + 8 + (cursor_col * char_width);
        float cursor_y = y + 4 + (cursor_line * LINE_HEIGHT);
        float cursor_h = LINE_HEIGHT - 2;
        float cursor_w = 2.0f;  /* | bar width */
        
        /* Blink: use frame time for simple toggle */
        static int blink_counter = 0;
        static bool cursor_visible = true;
        blink_counter++;
        if (blink_counter > 30) {  /* ~530ms at 60fps */
            blink_counter = 0;
            cursor_visible = !cursor_visible;
        }
        
        if (cursor_visible) {
            /* Cursor color: slightly brighter than text, blue accent */
            RLColor cursor_color = RL_HEX(0x569CD6);
            
            /* Draw | bar cursor with slight rounding */
            rl_graphics_fill_rounded_rect(gfx, 
                (RLRect){cursor_x, cursor_y + 1, cursor_w, cursor_h}, 
                cursor_color, 1.0f);
        }
    }
    
    /* Gutter separator */
    rl_graphics_draw_line(gfx, x + GUTTER_WIDTH, y, x + GUTTER_WIDTH, y + h, RL_HEX(0x3C3C3C));
}

/* Render status bar */
void rl_ui_render_statusbar(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h) {
    (void)app;
    
    /* Background */
    rl_graphics_fill_rect(gfx, (RLRect){x, y, w, h}, RL_ACCENT);
    
    /* Left side: cursor position */
    rl_graphics_draw_text(gfx, "Ln 1, Col 1", x + 12, y + 5, 12, RL_TEXT);
    
    /* Right side: language, encoding */
    rl_graphics_draw_text(gfx, "REOX", x + w - 150, y + 5, 12, RL_TEXT);
    rl_graphics_draw_text(gfx, "UTF-8", x + w - 80, y + 5, 12, RL_TEXT);
}
