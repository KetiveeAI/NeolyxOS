/*
 * Archive.app - NeolyxOS Native Archive Manager
 * Main application entry point
 * 
 * Uses nxrender_c for UI and nxzip for compression
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "nxgfx/nxgfx.h"
#include "widgets/widget.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/listview.h"
#include "widgets/progressbar.h"
#include "widgets/container.h"

#include <nxzip.h>
#include "archive.h"

/* Window dimensions */
#define WINDOW_WIDTH  900
#define WINDOW_HEIGHT 600
#define TOOLBAR_HEIGHT 48
#define SIDEBAR_WIDTH 200
#define STATUSBAR_HEIGHT 28

/* Colors (NeolyxOS dark theme) */
#define COL_BG          0x18181BFF
#define COL_TOOLBAR     0x27272AFF
#define COL_SIDEBAR     0x1F1F23FF
#define COL_LIST_BG     0x09090BFF
#define COL_LIST_HOVER  0x27272AFF
#define COL_LIST_SELECT 0x3F3F46FF
#define COL_ACCENT      0x6366F1FF
#define COL_TEXT        0xFAFAFAFF
#define COL_TEXT_DIM    0xA1A1AAFF
#define COL_BORDER      0x3F3F46FF

/* Application state */
typedef struct {
    SDL_Window* window;
    SDL_Surface* surface;
    int running;
    
    /* Archive state */
    archive_state_t archive;
    
    /* UI state */
    int hovered_item;
    int scroll_offset;
    int drag_scrollbar;
    
    /* Context menu */
    int menu_visible;
    int menu_x, menu_y;
} app_t;

static app_t app = {0};

/* Forward declarations */
static void render_toolbar(void);
static void render_file_list(void);
static void render_statusbar(void);
static void handle_mouse_click(int x, int y);
static void on_open_clicked(void);
static void on_extract_clicked(void);
static void on_add_clicked(void);

/* ------------------------------------------------------------------------ */
/* Rendering Helpers                                                         */
/* ------------------------------------------------------------------------ */

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
    SDL_Rect rect = {x, y, w, h};
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    SDL_FillRect(app.surface, &rect, SDL_MapRGB(app.surface->format, r, g, b));
}

static void draw_text(const char* text, int x, int y, uint32_t color) {
    (void)color;
    /* Placeholder - would use nxgfx_draw_text in real implementation */
    /* For now, text rendering requires font system */
    (void)text;
    (void)x;
    (void)y;
}

/* ------------------------------------------------------------------------ */
/* Toolbar                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    const char* label;
    const char* icon;
    void (*on_click)(void);
} toolbar_button_t;

static toolbar_button_t toolbar_buttons[] = {
    {"Open", "folder-open", on_open_clicked},
    {"Extract", "archive-extract", on_extract_clicked},
    {"Add", "list-add", on_add_clicked},
    {NULL, NULL, NULL}
};

static void render_toolbar(void) {
    fill_rect(0, 0, WINDOW_WIDTH, TOOLBAR_HEIGHT, COL_TOOLBAR);
    
    int x = 8;
    for (int i = 0; toolbar_buttons[i].label; i++) {
        int btn_w = 80;
        int btn_h = 32;
        int btn_y = (TOOLBAR_HEIGHT - btn_h) / 2;
        
        /* Button background */
        fill_rect(x, btn_y, btn_w, btn_h, COL_SIDEBAR);
        
        /* Button text would go here */
        draw_text(toolbar_buttons[i].label, x + 10, btn_y + 8, COL_TEXT);
        
        x += btn_w + 8;
    }
    
    /* Separator */
    fill_rect(0, TOOLBAR_HEIGHT - 1, WINDOW_WIDTH, 1, COL_BORDER);
    
    /* Archive name in center */
    if (app.archive.archive) {
        draw_text(app.archive.archive_name, WINDOW_WIDTH / 2 - 50, 14, COL_TEXT);
    }
}

/* ------------------------------------------------------------------------ */
/* File List                                                                 */
/* ------------------------------------------------------------------------ */

static void render_file_list(void) {
    int list_x = 0;
    int list_y = TOOLBAR_HEIGHT;
    int list_w = WINDOW_WIDTH;
    int list_h = WINDOW_HEIGHT - TOOLBAR_HEIGHT - STATUSBAR_HEIGHT;
    
    /* Background */
    fill_rect(list_x, list_y, list_w, list_h, COL_LIST_BG);
    
    if (!app.archive.archive) {
        /* Empty state */
        draw_text("Drop archive here or click Open", list_x + list_w/2 - 100, list_y + list_h/2, COL_TEXT_DIM);
        return;
    }
    
    /* List items */
    int row_height = 28;
    int visible_rows = list_h / row_height;
    int start_row = app.scroll_offset / row_height;
    
    for (int i = 0; i < visible_rows && (start_row + i) < app.archive.entry_count; i++) {
        int idx = start_row + i;
        archive_entry_t* entry = &app.archive.entries[idx];
        
        int row_y = list_y + i * row_height - (app.scroll_offset % row_height);
        
        /* Selection/hover background */
        if (idx == app.hovered_item) {
            fill_rect(list_x, row_y, list_w, row_height, COL_LIST_HOVER);
        }
        
        /* Icon (folder or file) */
        if (entry->is_directory) {
            fill_rect(list_x + 8, row_y + 6, 16, 12, 0x60A5FAFF);
        } else {
            fill_rect(list_x + 8, row_y + 4, 14, 16, 0x9CA3AFFF);
        }
        
        /* Name */
        draw_text(entry->name, list_x + 32, row_y + 6, COL_TEXT);
        
        /* Size */
        if (!entry->is_directory) {
            char size_str[32];
            snprintf(size_str, sizeof(size_str), "%s", format_file_size(entry->size));
            draw_text(size_str, list_w - 100, row_y + 6, COL_TEXT_DIM);
        }
        
        /* Separator */
        fill_rect(list_x, row_y + row_height - 1, list_w, 1, COL_BORDER);
    }
    
    /* Scrollbar */
    if (app.archive.entry_count > visible_rows) {
        int scrollbar_x = list_w - 8;
        int scrollbar_h = list_h * visible_rows / app.archive.entry_count;
        int scrollbar_y = list_y + (list_h - scrollbar_h) * app.scroll_offset / 
                          (app.archive.entry_count * row_height - list_h);
        
        fill_rect(scrollbar_x, scrollbar_y, 6, scrollbar_h, COL_SIDEBAR);
    }
}

/* ------------------------------------------------------------------------ */
/* Status Bar                                                                */
/* ------------------------------------------------------------------------ */

static void render_statusbar(void) {
    int y = WINDOW_HEIGHT - STATUSBAR_HEIGHT;
    fill_rect(0, y, WINDOW_WIDTH, STATUSBAR_HEIGHT, COL_TOOLBAR);
    fill_rect(0, y, WINDOW_WIDTH, 1, COL_BORDER);
    
    if (app.archive.archive) {
        char status[256];
        snprintf(status, sizeof(status), "%d items  |  %s  |  Compressed: %s",
                 app.archive.entry_count,
                 format_file_size(app.archive.total_size),
                 format_file_size(app.archive.compressed_size));
        draw_text(status, 12, y + 6, COL_TEXT_DIM);
    } else {
        draw_text("No archive open", 12, y + 6, COL_TEXT_DIM);
    }
}

/* ------------------------------------------------------------------------ */
/* Actions                                                                   */
/* ------------------------------------------------------------------------ */

static void on_open_clicked(void) {
    /* In real app, would open file dialog */
    /* For now, try to open a test file */
    const char* test_path = "/tmp/test.nxzip";
    if (archive_open(&app.archive, test_path)) {
        printf("Opened: %s (%d files)\n", test_path, app.archive.entry_count);
    }
}

static void on_extract_clicked(void) {
    if (!app.archive.archive) return;
    
    const char* dest = "/tmp/extracted";
    if (archive_extract_all(&app.archive, dest, NULL, NULL)) {
        printf("Extracted to: %s\n", dest);
    }
}

static void on_add_clicked(void) {
    /* Would open file picker */
}

/* ------------------------------------------------------------------------ */
/* Input Handling                                                            */
/* ------------------------------------------------------------------------ */

static void handle_mouse_click(int x, int y) {
    /* Toolbar buttons */
    if (y < TOOLBAR_HEIGHT) {
        int btn_x = 8;
        for (int i = 0; toolbar_buttons[i].label; i++) {
            int btn_w = 80;
            if (x >= btn_x && x < btn_x + btn_w) {
                if (toolbar_buttons[i].on_click) {
                    toolbar_buttons[i].on_click();
                }
                return;
            }
            btn_x += btn_w + 8;
        }
    }
    
    /* File list */
    if (y >= TOOLBAR_HEIGHT && y < WINDOW_HEIGHT - STATUSBAR_HEIGHT) {
        int row_height = 28;
        int row = (y - TOOLBAR_HEIGHT + app.scroll_offset) / row_height;
        if (row >= 0 && row < app.archive.entry_count) {
            app.hovered_item = row;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Main Loop                                                                 */
/* ------------------------------------------------------------------------ */

static void render(void) {
    fill_rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, COL_BG);
    
    render_toolbar();
    render_file_list();
    render_statusbar();
    
    SDL_UpdateWindowSurface(app.window);
}

static void handle_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                app.running = 0;
                break;
                
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    handle_mouse_click(e.button.x, e.button.y);
                }
                break;
                
            case SDL_MOUSEMOTION: {
                int y = e.motion.y;
                if (y >= TOOLBAR_HEIGHT && y < WINDOW_HEIGHT - STATUSBAR_HEIGHT) {
                    int row_height = 28;
                    app.hovered_item = (y - TOOLBAR_HEIGHT + app.scroll_offset) / row_height;
                }
                break;
            }
                
            case SDL_MOUSEWHEEL:
                app.scroll_offset -= e.wheel.y * 28;
                if (app.scroll_offset < 0) app.scroll_offset = 0;
                break;
                
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    app.running = 0;
                } else if (e.key.keysym.sym == SDLK_o && (e.key.keysym.mod & KMOD_CTRL)) {
                    on_open_clicked();
                }
                break;
                
            case SDL_DROPFILE: {
                char* path = e.drop.file;
                if (archive_open(&app.archive, path)) {
                    printf("Opened dropped file: %s\n", path);
                }
                SDL_free(path);
                break;
            }
        }
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    
    /* Create window */
    app.window = SDL_CreateWindow(
        "Archive-Pie",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );
    
    if (!app.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    
    app.surface = SDL_GetWindowSurface(app.window);
    app.running = 1;
    app.hovered_item = -1;
    
    /* Open file from command line */
    if (argc > 1) {
        archive_open(&app.archive, argv[1]);
    }
    
    /* Main loop */
    while (app.running) {
        handle_events();
        render();
        SDL_Delay(16);  /* ~60 FPS */
    }
    
    /* Cleanup */
    archive_close(&app.archive);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    
    return 0;
}
