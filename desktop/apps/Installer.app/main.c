/*
 * NeolyxOS Installer - Main Entry Point
 * Native application installer with wizard UI
 * 
 * Supports two modes:
 *   - NXRender mode: Full GUI (when linked with nxrender)
 *   - Text mode: Terminal fallback (for testing)
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/installer.h"

#ifdef NXRENDER_UI
/* NXRender GUI mode */
#include <nxgfx/nxgfx.h>
#include <nxrender/window.h>
#include <widgets/widget.h>
static nx_context_t* g_ctx = NULL;
static nx_font_t* g_font = NULL;
static nx_font_t* g_font_large = NULL;
#define USE_NXRENDER 1
#else
/* Text mode fallback */
#define USE_NXRENDER 0
#endif

/* UI Constants */
#define WINDOW_WIDTH  600
#define WINDOW_HEIGHT 450
#define PADDING       24
#define BUTTON_WIDTH  100
#define BUTTON_HEIGHT 36

/* Colors (RGBA) */
#define COL_BG        0x1C1C1EFF
#define COL_SURFACE   0x2C2C2EFF
#define COL_BORDER    0x48484AFF
#define COL_PRIMARY   0x007AFFFF
#define COL_TEXT      0xFFFFFFFF
#define COL_TEXT_SEC  0xAEAEB2FF
#define COL_SUCCESS   0x34C759FF
#define COL_ERROR     0xFF3B30FF

/* Convert RGBA to nx_color_t */
#if USE_NXRENDER
static inline nx_color_t col(uint32_t rgba) {
    return nx_rgba((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, 
                   (rgba >> 8) & 0xFF, rgba & 0xFF);
}
#endif

/* Forward declarations */
static void render_step_welcome(installer_state_t* state, int y);
static void render_step_permissions(installer_state_t* state, int y);
static void render_step_location(installer_state_t* state, int y);
static void render_step_progress(installer_state_t* state, int y);
static void render_step_complete(installer_state_t* state, int y);
static void handle_button_click(installer_state_t* state, int button_id);

/* Global state */
static installer_state_t g_state = {0};

/* Step titles */
static const char* step_titles[] = {
    "Welcome",
    "Permissions",
    "Install Location",
    "Installing...",
    "Complete"
};

/* Drawing abstractions - work in both modes */
static void draw_text(int x, int y, const char* text, uint32_t color, int size) {
#if USE_NXRENDER
    nx_font_t* font = (size > 14) ? g_font_large : g_font;
    if (g_ctx && font) {
        nxgfx_draw_text(g_ctx, text, (nx_point_t){x, y}, font, col(color));
    }
#else
    (void)x; (void)y; (void)color; (void)size;
    printf("[TEXT] %s\n", text);
#endif
}

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
#if USE_NXRENDER
    if (g_ctx) {
        nxgfx_fill_rect(g_ctx, nx_rect(x, y, w, h), col(color));
    }
#else
    (void)x; (void)y; (void)w; (void)h; (void)color;
#endif
}

static void draw_rounded_rect(int x, int y, int w, int h, uint32_t color, int radius) {
#if USE_NXRENDER
    if (g_ctx) {
        nxgfx_fill_rounded_rect(g_ctx, nx_rect(x, y, w, h), col(color), radius);
    }
#else
    (void)x; (void)y; (void)w; (void)h; (void)color; (void)radius;
#endif
}

static void draw_button(int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg) {
#if USE_NXRENDER
    draw_rounded_rect(x, y, w, h, bg, 6);
    /* Center text in button */
    int tx = x + (w / 2) - 30;
    int ty = y + (h / 2) - 6;
    draw_text(tx, ty, label, fg, 14);
#else
    (void)x; (void)y; (void)w; (void)h; (void)bg; (void)fg;
    printf("[BTN] %s\n", label);
#endif
}

static void draw_progress_bar(int x, int y, int w, int h, int percent) {
    int filled = (w * percent) / 100;
#if USE_NXRENDER
    draw_rounded_rect(x, y, w, h, COL_SURFACE, 4);
    if (filled > 0) {
        draw_rounded_rect(x, y, filled, h, COL_PRIMARY, 4);
    }
#else
    draw_rect(x, y, w, h, COL_SURFACE);
    draw_rect(x, y, filled, h, COL_PRIMARY);
    printf("[PROGRESS] %d%%\n", percent);
#endif
}

static void draw_checkbox(int x, int y, const char* label, bool checked) {
#if USE_NXRENDER
    if (checked) {
        draw_rounded_rect(x, y, 20, 20, COL_PRIMARY, 4);
    } else {
        nxgfx_draw_rect(g_ctx, nx_rect(x, y, 20, 20), col(COL_BORDER), 2);
    }
    draw_text(x + 30, y + 2, label, COL_TEXT, 14);
#else
    (void)x; (void)y;
    printf("[%c] %s\n", checked ? 'X' : ' ', label);
#endif
}

/* Render header with step indicator */
static void render_header(installer_state_t* state) {
    draw_rect(0, 0, WINDOW_WIDTH, 60, COL_SURFACE);
    draw_text(PADDING, 20, "NeolyxOS App Installer", COL_TEXT, 18);
    
    /* Step indicator */
    int step_x = WINDOW_WIDTH - PADDING - 200;
    char step_text[64];
    snprintf(step_text, sizeof(step_text), "Step %d of %d: %s", 
             state->current_step + 1, STEP_COUNT, step_titles[state->current_step]);
    draw_text(step_x, 20, step_text, COL_TEXT_SEC, 12);
}

/* Render footer with navigation buttons */
static void render_footer(installer_state_t* state) {
    int y = WINDOW_HEIGHT - 60;
    draw_rect(0, y, WINDOW_WIDTH, 60, COL_SURFACE);
    
    /* Cancel button (always visible except on complete) */
    if (state->current_step != STEP_COMPLETE) {
        draw_button(PADDING, y + 12, BUTTON_WIDTH, BUTTON_HEIGHT, 
                   "Cancel", COL_SURFACE, COL_TEXT);
    }
    
    /* Navigation buttons */
    int right_x = WINDOW_WIDTH - PADDING - BUTTON_WIDTH;
    
    switch (state->current_step) {
        case STEP_WELCOME:
            draw_button(right_x, y + 12, BUTTON_WIDTH, BUTTON_HEIGHT,
                       "Continue", COL_PRIMARY, COL_TEXT);
            break;
        case STEP_PERMISSIONS:
        case STEP_LOCATION:
            draw_button(right_x - BUTTON_WIDTH - 10, y + 12, BUTTON_WIDTH, BUTTON_HEIGHT,
                       "Back", COL_SURFACE, COL_TEXT);
            draw_button(right_x, y + 12, BUTTON_WIDTH, BUTTON_HEIGHT,
                       state->current_step == STEP_LOCATION ? "Install" : "Continue", 
                       COL_PRIMARY, COL_TEXT);
            break;
        case STEP_PROGRESS:
            /* No navigation during install */
            break;
        case STEP_COMPLETE:
            draw_button(right_x - BUTTON_WIDTH - 10, y + 12, BUTTON_WIDTH, BUTTON_HEIGHT,
                       "Open App", COL_PRIMARY, COL_TEXT);
            draw_button(right_x, y + 12, BUTTON_WIDTH, BUTTON_HEIGHT,
                       "Done", COL_SURFACE, COL_TEXT);
            break;
        default:
            break;
    }
}

/* Step 1: Welcome */
static void render_step_welcome(installer_state_t* state, int y) {
    if (!state->package) return;
    
    app_manifest_t* m = &state->package->manifest;
    
    /* App icon area */
    draw_rect(WINDOW_WIDTH/2 - 48, y, 96, 96, COL_SURFACE);
    draw_text(WINDOW_WIDTH/2 - 20, y + 35, "[ICON]", COL_TEXT_SEC, 14);
    y += 120;
    
    /* App name and version */
    char title[128];
    snprintf(title, sizeof(title), "Install \"%s\" v%s", m->name, m->version);
    draw_text(WINDOW_WIDTH/2 - 100, y, title, COL_TEXT, 20);
    y += 30;
    
    /* Author */
    char author[128];
    snprintf(author, sizeof(author), "by %s", m->author);
    draw_text(WINDOW_WIDTH/2 - 50, y, author, COL_TEXT_SEC, 14);
    y += 50;
    
    /* Description */
    draw_text(PADDING, y, m->description, COL_TEXT_SEC, 14);
    y += 40;
    
    /* Size info */
    char size[64];
    snprintf(size, sizeof(size), "Size: %.1f MB", state->package->total_size / (1024.0 * 1024.0));
    draw_text(PADDING, y, size, COL_TEXT_SEC, 12);
}

/* Step 2: Permissions */
static void render_step_permissions(installer_state_t* state, int y) {
    if (!state->package) return;
    
    draw_text(PADDING, y, "This application requires the following permissions:", COL_TEXT, 14);
    y += 40;
    
    uint32_t perms = state->package->manifest.permissions;
    
    if (perms & PERM_FILESYSTEM_READ) {
        draw_checkbox(PADDING, y, "Read files from disk", true);
        y += 30;
    }
    if (perms & PERM_FILESYSTEM_WRITE) {
        draw_checkbox(PADDING, y, "Write files to disk", true);
        y += 30;
    }
    if (perms & PERM_NETWORK) {
        draw_checkbox(PADDING, y, "Access the network", true);
        y += 30;
    }
    if (perms & PERM_CAMERA) {
        draw_checkbox(PADDING, y, "Use the camera", true);
        y += 30;
    }
    if (perms & PERM_MICROPHONE) {
        draw_checkbox(PADDING, y, "Use the microphone", true);
        y += 30;
    }
    if (perms & PERM_LOCATION) {
        draw_checkbox(PADDING, y, "Access your location", true);
        y += 30;
    }
    
    /* Warning if many permissions */
    if (__builtin_popcount(perms) > 4) {
        y += 20;
        draw_text(PADDING, y, "This app requests many permissions.", COL_ERROR, 12);
    }
}

/* Step 3: Install Location */
static void render_step_location(installer_state_t* state, int y) {
    draw_text(PADDING, y, "Choose where to install:", COL_TEXT, 14);
    y += 40;
    
    /* Radio buttons for location */
    bool is_default = strcmp(state->install_path, "/Applications") == 0;
    
    printf("(%c) /Applications (Recommended)\n", is_default ? '*' : ' ');
    y += 30;
    
    printf("( ) Custom location\n");
    y += 30;
    
    /* Path display */
    y += 20;
    draw_text(PADDING, y, "Install path:", COL_TEXT_SEC, 12);
    y += 20;
    draw_rect(PADDING, y, WINDOW_WIDTH - PADDING*2, 36, COL_SURFACE);
    draw_text(PADDING + 10, y + 10, state->install_path, COL_TEXT, 14);
    
    /* Space info */
    y += 60;
    char space[128];
    snprintf(space, sizeof(space), "Space required: %.1f MB", 
             state->package ? state->package->total_size / (1024.0 * 1024.0) : 0);
    draw_text(PADDING, y, space, COL_TEXT_SEC, 12);
    y += 20;
    draw_text(PADDING, y, "Space available: 128 GB", COL_TEXT_SEC, 12);
}

/* Step 4: Installation Progress */
static void render_step_progress(installer_state_t* state, int y) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Installing %s...", 
             state->package ? state->package->manifest.name : "application");
    draw_text(PADDING, y, msg, COL_TEXT, 16);
    y += 50;
    
    /* Progress bar */
    draw_progress_bar(PADDING, y, WINDOW_WIDTH - PADDING*2, 24, state->install_progress);
    y += 50;
    
    /* Current file */
    draw_text(PADDING, y, state->status_message, COL_TEXT_SEC, 12);
}

/* Step 5: Complete */
static void render_step_complete(installer_state_t* state, int y) {
    if (state->install_success) {
        draw_text(WINDOW_WIDTH/2 - 80, y, "Installation Complete!", COL_SUCCESS, 20);
        y += 50;
        
        char path[256];
        snprintf(path, sizeof(path), "%s has been installed to:", 
                 state->package ? state->package->manifest.name : "Application");
        draw_text(PADDING, y, path, COL_TEXT, 14);
        y += 25;
        draw_text(PADDING, y, state->install_path, COL_TEXT_SEC, 12);
        y += 60;
        
        /* Options */
        draw_checkbox(PADDING, y, "Add to Dock", state->add_to_dock);
        y += 30;
        draw_checkbox(PADDING, y, "Create Desktop shortcut", state->create_shortcut);
    } else {
        draw_text(WINDOW_WIDTH/2 - 80, y, "Installation Failed", COL_ERROR, 20);
        y += 50;
        draw_text(PADDING, y, state->status_message, COL_ERROR, 14);
    }
}

/* Main render function */
static void render(installer_state_t* state) {
    /* Clear background */
    draw_rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, COL_BG);
    
    /* Header */
    render_header(state);
    
    /* Content area */
    int content_y = 80;
    
    switch (state->current_step) {
        case STEP_WELCOME:
            render_step_welcome(state, content_y);
            break;
        case STEP_PERMISSIONS:
            render_step_permissions(state, content_y);
            break;
        case STEP_LOCATION:
            render_step_location(state, content_y);
            break;
        case STEP_PROGRESS:
            render_step_progress(state, content_y);
            break;
        case STEP_COMPLETE:
            render_step_complete(state, content_y);
            break;
        default:
            break;
    }
    
    /* Footer */
    render_footer(state);
    
    printf("\n---\n");
}

/* Progress callback for extraction */
static void progress_callback(int percent, const char* file) {
    g_state.install_progress = percent;
    snprintf(g_state.status_message, sizeof(g_state.status_message), 
             "Extracting: %s", file);
    render(&g_state);
}

/* Perform installation */
static bool do_install(installer_state_t* state) {
    if (!state->package) return false;
    
    state->current_step = STEP_PROGRESS;
    state->install_progress = 0;
    render(state);
    
    /* Simulate extraction progress */
    for (int i = 0; i <= 100; i += 10) {
        state->install_progress = i;
        snprintf(state->status_message, sizeof(state->status_message),
                 "Copying files... %d%%", i);
        render(state);
    }
    
    /* Register app */
    state->install_progress = 100;
    snprintf(state->status_message, sizeof(state->status_message),
             "Registering application...");
    render(state);
    
    state->install_success = true;
    state->current_step = STEP_COMPLETE;
    return true;
}

/* Handle navigation */
static void handle_button_click(installer_state_t* state, int button_id) {
    switch (button_id) {
        case 0: /* Cancel */
            printf("Installation cancelled.\n");
            exit(0);
            break;
        case 1: /* Back */
            if (state->current_step > STEP_WELCOME) {
                state->current_step--;
            }
            break;
        case 2: /* Continue/Install */
            if (state->current_step == STEP_LOCATION) {
                do_install(state);
            } else if (state->current_step < STEP_PROGRESS) {
                state->current_step++;
            }
            break;
        case 3: /* Done */
            printf("Installation complete. Exiting.\n");
            exit(0);
            break;
        default:
            break;
    }
}

/* Create demo package for testing */
static package_t* create_demo_package(void) {
    package_t* pkg = calloc(1, sizeof(package_t));
    if (!pkg) return NULL;
    
    strcpy(pkg->path, "demo.nxpkg");
    pkg->is_valid = true;
    pkg->file_count = 12;
    pkg->total_size = 4200000;
    
    /* Demo manifest */
    strcpy(pkg->manifest.name, "Calculator");
    strcpy(pkg->manifest.version, "1.0.0");
    strcpy(pkg->manifest.bundle_id, "com.neolyx.calculator");
    strcpy(pkg->manifest.category, "Utilities");
    strcpy(pkg->manifest.description, 
           "A beautiful calculator app for NeolyxOS with scientific functions.");
    strcpy(pkg->manifest.author, "KetiveeAI");
    strcpy(pkg->manifest.binary, "bin/calculator");
    strcpy(pkg->manifest.icon, "resources/calculator.nxi");
    pkg->manifest.permissions = PERM_FILESYSTEM_READ | PERM_FILESYSTEM_WRITE;
    pkg->manifest.size_bytes = 4200000;
    
    return pkg;
}

/* Main entry point */
int main(int argc, char* argv[]) {
    printf("=== NeolyxOS App Installer v%s ===\n\n", INSTALLER_VERSION);
    
    /* Initialize state */
    memset(&g_state, 0, sizeof(g_state));
    g_state.current_step = STEP_WELCOME;
    strcpy(g_state.install_path, "/Applications");
    g_state.add_to_dock = true;
    g_state.create_shortcut = false;
    
    /* Load package */
    if (argc > 1) {
        g_state.package = package_open(argv[1]);
        if (!g_state.package) {
            printf("Error: Could not open package: %s\n", argv[1]);
            return 1;
        }
    } else {
        /* Demo mode */
        printf("[Demo mode - no package specified]\n\n");
        g_state.package = create_demo_package();
    }
    
    /* Run wizard (text mode for now) */
    printf("Press Enter to continue through each step...\n\n");
    
    while (g_state.current_step < STEP_COUNT) {
        render(&g_state);
        
        /* Wait for input */
        getchar();
        
        /* Advance */
        if (g_state.current_step == STEP_LOCATION) {
            do_install(&g_state);
        } else if (g_state.current_step < STEP_COMPLETE) {
            g_state.current_step++;
        } else {
            break;
        }
    }
    
    /* Cleanup */
    if (g_state.package) {
        free(g_state.package);
    }
    
    return 0;
}
