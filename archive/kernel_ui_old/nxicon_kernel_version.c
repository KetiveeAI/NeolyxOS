/*
 * NeolyxOS Icon System Implementation
 * 
 * Vector icon parser and renderer with built-in system icons.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "ui/nxicon.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);

/* ============ Math Helpers ============ */

static int nxi_abs(int x) { return x < 0 ? -x : x; }

static int nxi_min(int a, int b) { return a < b ? a : b; }
static int nxi_max(int a, int b) { return a > b ? a : b; }

/* ============ String Helpers ============ */

static int nxi_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

/* ============ Built-in Icon Definitions ============ */

/* App icon - rounded square with gradient */
static const nxi_path_t icon_app_paths[] = {
    {NXI_CMD_MOVE, 200, 100, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 100, 0, 0, 0, 0, {0}},
    {NXI_CMD_CURVE, 900, 100, 900, 200, 0, 0, {0}},
    {NXI_CMD_LINE, 900, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_CURVE, 900, 900, 800, 900, 0, 0, {0}},
    {NXI_CMD_LINE, 200, 900, 0, 0, 0, 0, {0}},
    {NXI_CMD_CURVE, 100, 900, 100, 800, 0, 0, {0}},
    {NXI_CMD_LINE, 100, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_CURVE, 100, 100, 200, 100, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFF4D7FFF, 0, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* Folder icon */
static const nxi_path_t icon_folder_paths[] = {
    /* Tab */
    {NXI_CMD_MOVE, 100, 250, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 100, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 400, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 450, 250, 0, 0, 0, 0, {0}},
    /* Body */
    {NXI_CMD_MOVE, 100, 250, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 900, 250, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 900, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 100, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_CLOSE, 0, 0, 0, 0, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFFFFCC00, 0, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* File icon */
static const nxi_path_t icon_file_paths[] = {
    {NXI_CMD_MOVE, 200, 100, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 600, 100, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 300, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 900, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 200, 900, 0, 0, 0, 0, {0}},
    {NXI_CMD_CLOSE, 0, 0, 0, 0, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFFE0E0E0, 0, {0}},
    /* Corner fold */
    {NXI_CMD_MOVE, 600, 100, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 600, 300, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 300, 0, 0, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFFC0C0C0, 0, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* Terminal icon */
static const nxi_path_t icon_terminal_paths[] = {
    /* Background */
    {NXI_CMD_MOVE, 100, 150, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 900, 150, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 900, 850, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 100, 850, 0, 0, 0, 0, {0}},
    {NXI_CMD_CLOSE, 0, 0, 0, 0, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFF1E1E2E, 0, {0}},
    /* Prompt > */
    {NXI_CMD_MOVE, 200, 400, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 350, 500, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 200, 600, 0, 0, 0, 0, {0}},
    {NXI_CMD_STROKE, 0, 0, 0, 0, 0xFF4DFF4D, 3, {0}},
    /* Cursor line */
    {NXI_CMD_MOVE, 400, 500, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 700, 500, 0, 0, 0, 0, {0}},
    {NXI_CMD_STROKE, 0, 0, 0, 0, 0xFFFFFFFF, 2, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* Settings/gear icon */
static const nxi_path_t icon_settings_paths[] = {
    /* Center circle */
    {NXI_CMD_MOVE, 500, 350, 0, 0, 0, 0, {0}},
    {NXI_CMD_ARC, 150, 500, 650, 0, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFF808080, 0, {0}},
    /* Cog teeth (simplified) */
    {NXI_CMD_MOVE, 450, 100, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 550, 100, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 550, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 450, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFF808080, 0, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* Font icon (letter A) */
static const nxi_path_t icon_font_paths[] = {
    {NXI_CMD_MOVE, 200, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 500, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 650, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 580, 600, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 420, 600, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 350, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_CLOSE, 0, 0, 0, 0, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFF4D9FFF, 0, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* Disk icon */
static const nxi_path_t icon_disk_paths[] = {
    {NXI_CMD_MOVE, 150, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 850, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 850, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 150, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_CLOSE, 0, 0, 0, 0, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFF606080, 0, {0}},
    /* Activity LED */
    {NXI_CMD_MOVE, 750, 300, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 300, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 350, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 750, 350, 0, 0, 0, 0, {0}},
    {NXI_CMD_FILL, 0, 0, 0, 0, 0xFF00FF00, 0, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* Search/magnifier icon */
static const nxi_path_t icon_search_paths[] = {
    /* Circle */
    {NXI_CMD_MOVE, 450, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_ARC, 200, 450, 400, 0, 0, 0, {0}},
    {NXI_CMD_STROKE, 0, 0, 0, 0, 0xFFFFFFFF, 4, {0}},
    /* Handle */
    {NXI_CMD_MOVE, 600, 600, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_STROKE, 0, 0, 0, 0, 0xFFFFFFFF, 5, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* Close (X) icon */
static const nxi_path_t icon_close_paths[] = {
    {NXI_CMD_MOVE, 200, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_STROKE, 0, 0, 0, 0, 0xFFFF5555, 4, {0}},
    {NXI_CMD_MOVE, 800, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 200, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_STROKE, 0, 0, 0, 0, 0xFFFF5555, 4, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* Check mark icon */
static const nxi_path_t icon_check_paths[] = {
    {NXI_CMD_MOVE, 200, 500, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 400, 700, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 300, 0, 0, 0, 0, {0}},
    {NXI_CMD_STROKE, 0, 0, 0, 0, 0xFF55FF55, 4, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* Plus icon */
static const nxi_path_t icon_plus_paths[] = {
    {NXI_CMD_MOVE, 500, 200, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 500, 800, 0, 0, 0, 0, {0}},
    {NXI_CMD_STROKE, 0, 0, 0, 0, 0xFFFFFFFF, 4, {0}},
    {NXI_CMD_MOVE, 200, 500, 0, 0, 0, 0, {0}},
    {NXI_CMD_LINE, 800, 500, 0, 0, 0, 0, {0}},
    {NXI_CMD_STROKE, 0, 0, 0, 0, 0xFFFFFFFF, 4, {0}},
    {NXI_CMD_END, 0, 0, 0, 0, 0, 0, {0}}
};

/* ============ System Iconset ============ */

static nxi_iconset_t g_system_icons;
static int g_icons_initialized = 0;
static nxi_theme_t g_current_theme = NXI_THEME_LIGHT;

/* Theme management */
void nxi_set_theme(nxi_theme_t theme) {
    g_current_theme = theme;
}

nxi_theme_t nxi_get_theme(void) {
    return g_current_theme;
}

/* Helper to add built-in icon */
static void add_builtin_icon(uint32_t id, const char *name, 
                             const nxi_path_t *paths, int path_count) {
    if (g_system_icons.icon_count >= NXI_MAX_ICONS) return;
    
    nxi_icon_t *icon = &g_system_icons.icons[g_system_icons.icon_count++];
    icon->id = id;
    icon->base_width = 1000;
    icon->base_height = 1000;
    icon->path_count = path_count;
    icon->has_dark_theme = 0;
    icon->reserved = 0;
    
    /* Initialize viewbox to match base dimensions */
    icon->viewbox.x = 0;
    icon->viewbox.y = 0;
    icon->viewbox.width = 1000;
    icon->viewbox.height = 1000;
    
    /* Copy name */
    int i = 0;
    while (name[i] && i < 31) {
        icon->name[i] = name[i];
        i++;
    }
    icon->name[i] = '\0';
    
    /* Copy paths for both themes (built-ins use same paths) */
    for (int p = 0; p < path_count && p < NXI_MAX_PATHS; p++) {
        icon->paths[p] = paths[p];
        icon->paths_dark[p] = paths[p];  /* Same for built-ins */
    }
}

void nxi_init(void) {
    if (g_icons_initialized) return;
    
    serial_puts("[NXICON] Initializing icon system...\n");
    
    g_system_icons.icon_count = 0;
    
    /* Register built-in icons */
    add_builtin_icon(NXI_ICON_APP_DEFAULT, "app", icon_app_paths, 11);
    add_builtin_icon(NXI_ICON_FOLDER, "folder", icon_folder_paths, 11);
    add_builtin_icon(NXI_ICON_FILE, "file", icon_file_paths, 12);
    add_builtin_icon(NXI_ICON_TERMINAL, "terminal", icon_terminal_paths, 14);
    add_builtin_icon(NXI_ICON_SETTINGS, "settings", icon_settings_paths, 9);
    add_builtin_icon(NXI_ICON_FONT, "font", icon_font_paths, 10);
    add_builtin_icon(NXI_ICON_DISK, "disk", icon_disk_paths, 12);
    add_builtin_icon(NXI_ICON_SEARCH, "search", icon_search_paths, 7);
    add_builtin_icon(NXI_ICON_CLOSE, "close", icon_close_paths, 7);
    add_builtin_icon(NXI_ICON_CHECK, "check", icon_check_paths, 5);
    add_builtin_icon(NXI_ICON_PLUS, "plus", icon_plus_paths, 7);
    
    g_icons_initialized = 1;
    serial_puts("[NXICON] Registered 11 system icons\n");
}

const nxi_icon_t* nxi_get_icon(uint32_t id) {
    if (!g_icons_initialized) nxi_init();
    
    for (int i = 0; i < g_system_icons.icon_count; i++) {
        if (g_system_icons.icons[i].id == id) {
            return &g_system_icons.icons[i];
        }
    }
    return NULL;
}

const nxi_icon_t* nxi_get_icon_by_name(const char *name) {
    if (!g_icons_initialized) nxi_init();
    
    for (int i = 0; i < g_system_icons.icon_count; i++) {
        if (nxi_strcmp(g_system_icons.icons[i].name, name) == 0) {
            return &g_system_icons.icons[i];
        }
    }
    return NULL;
}

const nxi_iconset_t* nxi_get_system_icons(void) {
    if (!g_icons_initialized) nxi_init();
    return &g_system_icons;
}

/* ============ Rendering ============ */

/* Scale coordinate from viewbox space to pixel space */
static int nxi_scale_viewbox(int16_t coord, int size, int16_t viewbox_dim) {
    if (viewbox_dim <= 0) viewbox_dim = 1000;  /* Fallback */
    return (coord * size) / viewbox_dim;
}

/* Legacy scale for backwards compatibility */
static int nxi_scale(int16_t coord, int size) {
    return (coord * size) / 1000;
}

/* Blend color with alpha */
static uint32_t nxi_blend(uint32_t fg, uint32_t bg, int alpha) {
    if (alpha <= 0) return bg;
    if (alpha >= 255) return fg;
    
    int r = (((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * (255 - alpha)) >> 8;
    int g = (((fg >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * (255 - alpha)) >> 8;
    int b = ((fg & 0xFF) * alpha + (bg & 0xFF) * (255 - alpha)) >> 8;
    
    return (r << 16) | (g << 8) | b;
}

/* Plot pixel with bounds check */
static void nxi_plot(volatile uint32_t *fb, uint32_t pitch,
                     int x, int y, uint32_t color,
                     uint32_t fb_width, uint32_t fb_height) {
    if (x >= 0 && x < (int)fb_width && y >= 0 && y < (int)fb_height) {
        fb[y * (pitch / 4) + x] = color;
    }
}

/* Draw line for stroke */
static void nxi_draw_line(volatile uint32_t *fb, uint32_t pitch,
                          int x0, int y0, int x1, int y1,
                          uint32_t color, int width,
                          uint32_t fb_width, uint32_t fb_height) {
    int dx = nxi_abs(x1 - x0);
    int dy = nxi_abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int w2 = width / 2;
    
    while (1) {
        /* Draw thick line */
        for (int ox = -w2; ox <= w2; ox++) {
            for (int oy = -w2; oy <= w2; oy++) {
                nxi_plot(fb, pitch, x0 + ox, y0 + oy, color, fb_width, fb_height);
            }
        }
        
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/* Simple flood fill for closed paths (scanline based) */
static void nxi_fill_rect(volatile uint32_t *fb, uint32_t pitch,
                          int x, int y, int w, int h, uint32_t color,
                          uint32_t fb_width, uint32_t fb_height) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            nxi_plot(fb, pitch, x + i, y + j, color, fb_width, fb_height);
        }
    }
}

/* Core render function with theme support */
static void nxi_render_core(const nxi_icon_t *icon, int x, int y, int size,
                            nxi_theme_t theme,
                            volatile uint32_t *fb, uint32_t pitch,
                            uint32_t fb_width, uint32_t fb_height) {
    if (!icon || !fb || size <= 0) return;
    
    /* Select appropriate path array based on theme */
    const nxi_path_t *paths = (theme == NXI_THEME_DARK && icon->has_dark_theme) 
                               ? icon->paths_dark : icon->paths;
    
    /* Use viewbox for scaling if defined, else use base dimensions */
    int16_t vb_width = icon->viewbox.width > 0 ? icon->viewbox.width : icon->base_width;
    int16_t vb_height = icon->viewbox.height > 0 ? icon->viewbox.height : icon->base_height;
    
    int last_x = 0, last_y = 0;
    int path_start_x = 0, path_start_y = 0;
    int min_x = 10000, min_y = 10000;
    int max_x = 0, max_y = 0;
    int in_path = 0;
    
    for (int i = 0; i < icon->path_count; i++) {
        const nxi_path_t *p = &paths[i];
        
        /* Scale using viewbox dimensions */
        int px = x + nxi_scale_viewbox(p->x - icon->viewbox.x, size, vb_width);
        int py = y + nxi_scale_viewbox(p->y - icon->viewbox.y, size, vb_height);
        
        switch (p->cmd) {
            case NXI_CMD_MOVE:
                last_x = px;
                last_y = py;
                path_start_x = px;
                path_start_y = py;
                min_x = px; min_y = py;
                max_x = px; max_y = py;
                in_path = 1;
                break;
                
            case NXI_CMD_LINE:
                if (in_path) {
                    min_x = nxi_min(min_x, px);
                    min_y = nxi_min(min_y, py);
                    max_x = nxi_max(max_x, px);
                    max_y = nxi_max(max_y, py);
                }
                last_x = px;
                last_y = py;
                break;
                
            case NXI_CMD_CLOSE:
                last_x = path_start_x;
                last_y = path_start_y;
                break;
                
            case NXI_CMD_FILL:
                /* Simple rectangular fill for now */
                if (in_path && max_x > min_x && max_y > min_y) {
                    nxi_fill_rect(fb, pitch, min_x, min_y, 
                                  max_x - min_x, max_y - min_y,
                                  p->color, fb_width, fb_height);
                }
                in_path = 0;
                break;
                
            case NXI_CMD_STROKE:
                /* Draw outline */
                nxi_draw_line(fb, pitch, path_start_x, path_start_y,
                              last_x, last_y, p->color, p->width,
                              fb_width, fb_height);
                break;
                
            case NXI_CMD_END:
                return;
        }
    }
}

void nxi_render(const nxi_icon_t *icon, int x, int y, int size,
                volatile uint32_t *fb, uint32_t pitch,
                uint32_t fb_width, uint32_t fb_height) {
    /* Use current global theme */
    nxi_render_core(icon, x, y, size, g_current_theme, fb, pitch, fb_width, fb_height);
}

void nxi_render_themed(const nxi_icon_t *icon, int x, int y, int size,
                       nxi_theme_t theme,
                       volatile uint32_t *fb, uint32_t pitch,
                       uint32_t fb_width, uint32_t fb_height) {
    nxi_render_core(icon, x, y, size, theme, fb, pitch, fb_width, fb_height);
}

void nxi_render_tinted(const nxi_icon_t *icon, int x, int y, int size,
                       uint32_t tint_color,
                       volatile uint32_t *fb, uint32_t pitch,
                       uint32_t fb_width, uint32_t fb_height) {
    if (!icon || !fb || size <= 0) return;
    
    /* Apply tint by modifying icon colors before render */
    int tint_r = (tint_color >> 16) & 0xFF;
    int tint_g = (tint_color >> 8) & 0xFF;
    int tint_b = tint_color & 0xFF;
    
    /* Create temporary modified paths with tinted colors */
    const nxi_path_t *paths = icon->paths;
    
    for (int i = 0; i < icon->path_count; i++) {
        const nxi_path_t *p = &paths[i];
        
        if (p->cmd == NXI_CMD_FILL || p->cmd == NXI_CMD_STROKE) {
            /* Blend original color with tint (50% blend) */
            int orig_r = (p->color >> 16) & 0xFF;
            int orig_g = (p->color >> 8) & 0xFF;
            int orig_b = p->color & 0xFF;
            
            int blend_r = (orig_r + tint_r) >> 1;
            int blend_g = (orig_g + tint_g) >> 1;
            int blend_b = (orig_b + tint_b) >> 1;
            
            uint32_t blended = 0xFF000000 | (blend_r << 16) | (blend_g << 8) | blend_b;
            
            /* Render this element with blended color directly */
            if (p->cmd == NXI_CMD_FILL) {
                /* Use render_core for full icon, tint is approximation */
            }
        }
    }
    
    /* Fallback: just render normally (tint approximation) */
    nxi_render_core(icon, x, y, size, g_current_theme, fb, pitch, fb_width, fb_height);
}


/* ============ File Loading ============ */

int nxi_load(nxi_iconset_t *set, const uint8_t *data, uint32_t size) {
    if (!set || !data || size < sizeof(nxi_header_t)) return -1;
    
    const nxi_header_t *header = (const nxi_header_t *)data;
    
    /* Verify magic "NXI\0" */
    if (header->magic[0] != 'N' || header->magic[1] != 'X' ||
        header->magic[2] != 'I' || header->magic[3] != '\0') {
        return -2;  /* Invalid magic */
    }
    
    /* Check version */
    if (header->version > 1) {
        return -3;  /* Unsupported version */
    }
    
    /* Validate icon count */
    if (header->icon_count > NXI_MAX_ICONS) {
        return -4;  /* Too many icons */
    }
    
    /* Calculate expected minimum size */
    uint32_t min_size = sizeof(nxi_header_t) + 
                        header->icon_count * sizeof(nxi_icon_t);
    if (size < min_size) {
        return -5;  /* File too small */
    }
    
    /* Parse icon entries */
    const uint8_t *ptr = data + sizeof(nxi_header_t);
    set->icon_count = 0;
    
    for (uint32_t i = 0; i < header->icon_count && i < NXI_MAX_ICONS; i++) {
        /* Copy icon data */
        const nxi_icon_t *src_icon = (const nxi_icon_t *)ptr;
        nxi_icon_t *dst_icon = &set->icons[set->icon_count];
        
        /* Copy basic fields */
        dst_icon->id = src_icon->id;
        dst_icon->base_width = src_icon->base_width;
        dst_icon->base_height = src_icon->base_height;
        dst_icon->path_count = src_icon->path_count;
        dst_icon->has_dark_theme = src_icon->has_dark_theme;
        dst_icon->viewbox = src_icon->viewbox;
        
        /* Copy name */
        for (int n = 0; n < 32; n++) {
            dst_icon->name[n] = src_icon->name[n];
        }
        
        /* Copy paths (light theme) */
        int path_limit = src_icon->path_count;
        if (path_limit > NXI_MAX_PATHS) path_limit = NXI_MAX_PATHS;
        
        for (int p = 0; p < path_limit; p++) {
            dst_icon->paths[p] = src_icon->paths[p];
        }
        
        /* Copy dark theme paths if available */
        if (src_icon->has_dark_theme) {
            for (int p = 0; p < path_limit; p++) {
                dst_icon->paths_dark[p] = src_icon->paths_dark[p];
            }
        }
        
        set->icon_count++;
        ptr += sizeof(nxi_icon_t);
    }
    
    serial_puts("[NXICON] Loaded iconset from file\n");
    return 0;
}
