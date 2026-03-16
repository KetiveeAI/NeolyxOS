/*
 * NeolyxOS - NXI Icon Renderer
 * 
 * Renders NXI icons with:
 * 1. File-based loading from /System/Icons/*.nxi
 * 2. Icon caching for performance
 * 3. Procedural builtin fallbacks
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include "../include/nxi_render.h"
#include "../include/nxrender_bridge.h"
#include "../include/nxsyscall.h"
#include <stddef.h>

/* ============ NXI File Format (Simplified Bitmap Variant) ============ */

/* For desktop icons, we use a simplified NXI bitmap format:
 * - Header: magic[4] + version(1) + width(2) + height(2) + flags(1) = 10 bytes
 * - Pixel data: RGBA32, width * height * 4 bytes
 * 
 * This allows fast loading without vector rasterization.
 * IconLay.app exports both vector NXI and this pre-rendered format.
 */

#define NXI_BITMAP_MAGIC "NXIB"
#define NXI_BITMAP_VERSION 1

typedef struct {
    char magic[4];       /* "NXIB" */
    uint8_t version;     /* 1 */
    uint16_t width;
    uint16_t height;
    uint8_t flags;       /* 0=RGBA, 1=RGB */
} __attribute__((packed)) nxi_bitmap_header_t;

/* ============ Icon Cache ============ */

#define NXI_ICON_CACHE_SIZE 64
#define NXI_MAX_ICON_SIZE   128  /* Max cached icon size */

typedef struct {
    uint32_t icon_id;
    uint16_t size;        /* Requested size */
    uint16_t actual_w;    /* Actual width from file */
    uint16_t actual_h;    /* Actual height from file */
    uint8_t *rgba;        /* RGBA pixel data (allocated via nx_malloc) */
    uint8_t loaded;       /* 1=from file, 2=file not found (use procedural) */
} nxi_cached_icon_t;

static nxi_cached_icon_t g_icon_cache[NXI_ICON_CACHE_SIZE];
static int g_cache_count = 0;
static int g_nxi_initialized = 0;

/* System paths */
#define NXI_ICON_PATH "/System/Desktop/Icons/"

/* ============ Icon ID to Filename Mapping ============ */

static const char* nxi_get_icon_filename(uint32_t icon_id) {
    switch (icon_id) {
        case NXI_ICON_WIFI:       return "wifi";
        case NXI_ICON_BLUETOOTH:  return "bluetooth";
        case NXI_ICON_AIRDROP:    return "share";
        case NXI_ICON_ETHERNET:   return "ethernet";
        case NXI_ICON_BRIGHTNESS: return "brightness";
        case NXI_ICON_VOLUME:     return "volume";
        case NXI_ICON_FOCUS:      return "focus";
        case NXI_ICON_DARK_MODE:  return "darkmode";
        case NXI_ICON_MIRROR:     return "mirror";
        case NXI_ICON_KEYBOARD:   return "keyboard";
        case NXI_ICON_PLAY:       return "play";
        case NXI_ICON_PAUSE:      return "pause";
        case NXI_ICON_PREV:       return "prev";
        case NXI_ICON_NEXT:       return "next";
        case NXI_ICON_BELL:       return "bell";
        case NXI_ICON_GRID:       return "grid";
        case NXI_ICON_SETTINGS:   return "settings";
        case NXI_ICON_TERMINAL:   return "terminal";
        case NXI_ICON_FOLDER:     return "folder";
        case NXI_ICON_SEARCH:     return "search";
        case NXI_ICON_CLOSE:      return "close";
        case NXI_ICON_APP:        return "app";
        case NXI_ICON_FILE:       return "file";
        default: return NULL;
    }
}

/* Build path: /System/Icons/{name}_{size}.nxi */
static int nxi_build_path(char *buf, int buflen, const char *name, int size) {
    int i = 0;
    const char *prefix = NXI_ICON_PATH;
    
    while (prefix[i] && i < buflen - 1) {
        buf[i] = prefix[i];
        i++;
    }
    
    int j = 0;
    while (name[j] && i < buflen - 1) {
        buf[i++] = name[j++];
    }
    
    buf[i++] = '_';
    
    /* Size to string */
    if (size >= 100) buf[i++] = '0' + (size / 100) % 10;
    if (size >= 10) buf[i++] = '0' + (size / 10) % 10;
    buf[i++] = '0' + size % 10;
    
    buf[i++] = '.';
    buf[i++] = 'n';
    buf[i++] = 'x';
    buf[i++] = 'i';
    buf[i] = '\0';
    
    return i;
}

/* ============ File-based Icon Loading ============ */

static nxi_cached_icon_t* nxi_cache_find(uint32_t icon_id, int size) {
    for (int i = 0; i < g_cache_count; i++) {
        if (g_icon_cache[i].icon_id == icon_id && 
            g_icon_cache[i].size == (uint16_t)size) {
            return &g_icon_cache[i];
        }
    }
    return NULL;
}

static nxi_cached_icon_t* nxi_cache_alloc(uint32_t icon_id, int size) {
    if (g_cache_count >= NXI_ICON_CACHE_SIZE) {
        /* Evict oldest entry */
        if (g_icon_cache[0].rgba) {
            nx_mfree(g_icon_cache[0].rgba);
        }
        for (int i = 0; i < NXI_ICON_CACHE_SIZE - 1; i++) {
            g_icon_cache[i] = g_icon_cache[i + 1];
        }
        g_cache_count = NXI_ICON_CACHE_SIZE - 1;
    }
    
    nxi_cached_icon_t *entry = &g_icon_cache[g_cache_count++];
    entry->icon_id = icon_id;
    entry->size = (uint16_t)size;
    entry->rgba = NULL;
    entry->loaded = 0;
    return entry;
}

/* Try to load icon from /System/Icons/ */
static int nxi_load_from_file(nxi_cached_icon_t *entry, uint32_t icon_id, int size) {
    const char *name = nxi_get_icon_filename(icon_id);
    if (!name) {
        entry->loaded = 2;  /* No filename mapping */
        return -1;
    }
    
    char path[128];
    nxi_build_path(path, sizeof(path), name, size);
    
    /* Open file */
    int fd = nx_open(path, O_RDONLY);
    if (fd < 0) {
        /* Try without size suffix: /System/Icons/{name}.nxi */
        int i = 0;
        const char *prefix = NXI_ICON_PATH;
        while (prefix[i]) { path[i] = prefix[i]; i++; }
        int j = 0;
        while (name[j]) { path[i++] = name[j++]; }
        path[i++] = '.'; path[i++] = 'n'; path[i++] = 'x'; path[i++] = 'i'; path[i] = '\0';
        
        fd = nx_open(path, O_RDONLY);
        if (fd < 0) {
            entry->loaded = 2;  /* File not found */
            return -1;
        }
    }
    
    /* Read header */
    nxi_bitmap_header_t header;
    int64_t n = nx_read(fd, &header, sizeof(header));
    if (n != sizeof(header)) {
        nx_close(fd);
        entry->loaded = 2;
        return -1;
    }
    
    /* Validate header */
    if (header.magic[0] != 'N' || header.magic[1] != 'X' ||
        header.magic[2] != 'I' || header.magic[3] != 'B') {
        nx_close(fd);
        entry->loaded = 2;
        return -1;
    }
    
    /* Allocate pixel buffer */
    uint32_t pixel_count = header.width * header.height;
    uint32_t data_size = pixel_count * 4;
    
    entry->rgba = (uint8_t*)nx_malloc(data_size);
    if (!entry->rgba) {
        nx_close(fd);
        entry->loaded = 2;
        return -1;
    }
    
    /* Read pixel data */
    n = nx_read(fd, entry->rgba, data_size);
    nx_close(fd);
    
    if (n != (int64_t)data_size) {
        nx_mfree(entry->rgba);
        entry->rgba = NULL;
        entry->loaded = 2;
        return -1;
    }
    
    entry->actual_w = header.width;
    entry->actual_h = header.height;
    entry->loaded = 1;
    
    return 0;
}

/* ============ Procedural Icon Rendering ============ */

static void draw_builtin_icon(uint32_t icon_id, int x, int y, int sz, uint32_t color) {
    int cx = x + sz / 2;
    int cy = y + sz / 2;
    int r = sz / 3;
    
    switch (icon_id) {
    
    case NXI_ICON_WIFI: {
        for (int arc = 0; arc < 3; arc++) {
            int ar = sz/6 + arc * sz/6;
            for (int a = -45; a <= 45; a += 8) {
                int px = cx + (ar * a) / 90;
                int py = cy - ar + (ar * (45 - (a < 0 ? -a : a))) / 60;
                nxr_fill_circle(px, py, sz/16 + 1, color);
            }
        }
        nxr_fill_circle(cx, cy + sz/4, sz/10, color);
        break;
    }
    
    case NXI_ICON_BLUETOOTH: {
        int lw = sz / 12;
        nxr_fill_rect(cx - lw, y + sz/6, lw*2, sz*2/3, color);
        for (int i = 0; i < sz/4; i++) {
            nxr_fill_rect(cx + lw + i, y + sz/4 + i*2/3, lw, 1, color);
            nxr_fill_rect(cx + lw + i, y + sz*3/4 - i*2/3, lw, 1, color);
        }
        break;
    }
    
    case NXI_ICON_AIRDROP: {
        nxr_fill_circle(cx, y + sz/5, sz/8, color);
        nxr_fill_circle(x + sz/5, y + sz*3/4, sz/8, color);
        nxr_fill_circle(x + sz*4/5, y + sz*3/4, sz/8, color);
        nxr_draw_line(cx, y + sz/5, x + sz/5, y + sz*3/4, color);
        nxr_draw_line(cx, y + sz/5, x + sz*4/5, y + sz*3/4, color);
        break;
    }
    
    case NXI_ICON_ETHERNET: {
        nxr_fill_rounded_rect(x + sz/4, y + sz/4, sz/2, sz/2, sz/10, color);
        for (int i = 0; i < 3; i++) {
            nxr_fill_rect(x + sz/3 + i * sz/6 - 1, y + sz*3/4, 2, sz/6, color);
        }
        break;
    }
    
    case NXI_ICON_BRIGHTNESS: {
        nxr_fill_circle(cx, cy, r/2, color);
        for (int i = 0; i < 8; i++) {
            int dx = (i == 0 || i == 4) ? 0 : ((i < 4) ? 1 : -1) * r;
            int dy = (i == 2 || i == 6) ? 0 : ((i < 2 || i > 6) ? -1 : 1) * r;
            nxr_fill_circle(cx + dx * 3/4, cy + dy * 3/4, sz/12, color);
        }
        break;
    }
    
    case NXI_ICON_VOLUME: {
        nxr_fill_rect(x + sz/4, cy - sz/8, sz/6, sz/4, color);
        for (int i = 0; i < sz/4; i++) {
            int h = sz/8 + i * 2;
            nxr_fill_rect(x + sz/4 + sz/6 + i, cy - h/2, 2, h, color);
        }
        for (int w = 0; w < 2; w++) {
            int wr = sz/4 + w * sz/6;
            for (int a = -30; a <= 30; a += 10) {
                int px = x + sz/2 + wr * (45 - (a < 0 ? -a : a)) / 60;
                int py = cy + a * wr / 90;
                nxr_fill_circle(px, py, 1, color);
            }
        }
        break;
    }
    
    case NXI_ICON_FOCUS: {
        nxr_fill_circle(cx, cy, r, color);
        nxr_fill_circle(cx + r/2, cy - r/4, r * 2/3, 0xFF1C1C1E);
        break;
    }
    
    case NXI_ICON_DARK_MODE: {
        nxr_fill_circle(cx, cy, r, color);
        nxr_fill_rect(cx, y, sz/2, sz, 0xFF1C1C1E);
        break;
    }
    
    case NXI_ICON_MIRROR: {
        nxr_fill_rounded_rect(x + sz/8, y + sz/4, sz/3, sz/2, sz/16, color);
        nxr_fill_rounded_rect(x + sz/2, y + sz/5, sz/3, sz/2, sz/16, color);
        break;
    }
    
    case NXI_ICON_KEYBOARD: {
        nxr_fill_rounded_rect(x + sz/8, y + sz/3, sz * 3/4, sz/3, sz/12, color);
        for (int k = 0; k < 3; k++) {
            nxr_fill_rect(x + sz/4 + k * sz/5, y + sz/3 + sz/12, sz/8, sz/10, 0xFF1C1C1E);
        }
        break;
    }
    
    case NXI_ICON_PLAY: {
        for (int i = 0; i < sz/2; i++) {
            nxr_fill_rect(x + sz/4 + i/2, cy - i/2, 1, i + 1, color);
        }
        break;
    }
    
    case NXI_ICON_PAUSE: {
        nxr_fill_rect(x + sz/3 - sz/10, y + sz/4, sz/6, sz/2, color);
        nxr_fill_rect(x + sz*2/3 - sz/10, y + sz/4, sz/6, sz/2, color);
        break;
    }
    
    case NXI_ICON_PREV: {
        nxr_fill_rect(x + sz/6, y + sz/4, sz/12, sz/2, color);
        for (int i = 0; i < sz/4; i++) {
            nxr_fill_rect(x + sz/2 - i/2, cy - i/2, 1, i + 1, color);
        }
        break;
    }
    
    case NXI_ICON_NEXT: {
        for (int i = 0; i < sz/4; i++) {
            nxr_fill_rect(x + sz/3 + i/2, cy - i/2, 1, i + 1, color);
        }
        nxr_fill_rect(x + sz*5/6 - sz/12, y + sz/4, sz/12, sz/2, color);
        break;
    }
    
    case NXI_ICON_BELL: {
        nxr_fill_circle(cx, y + sz/3, sz/4, color);
        nxr_fill_rect(x + sz/3, y + sz/3, sz/3, sz/3, color);
        nxr_fill_circle(cx, y + sz*2/3 + 2, sz/3, color);
        nxr_fill_circle(cx, y + sz - sz/8, sz/10, color);
        break;
    }
    
    case NXI_ICON_GRID: {
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                int gx = x + sz/5 + col * sz/3;
                int gy = y + sz/5 + row * sz/3;
                nxr_fill_circle(gx, gy, sz/10, color);
            }
        }
        break;
    }
    
    case NXI_ICON_CLOSE: {
        for (int i = 0; i < sz/2; i++) {
            nxr_fill_rect(x + sz/4 + i, y + sz/4 + i, sz/8, sz/8, color);
            nxr_fill_rect(x + sz*3/4 - i - sz/8, y + sz/4 + i, sz/8, sz/8, color);
        }
        break;
    }
    
    case NXI_ICON_SETTINGS: {
        nxr_fill_circle(cx, cy, r, color);
        nxr_fill_circle(cx, cy, r/2, 0xFF1C1C1E);
        for (int i = 0; i < 6; i++) {
            int angle = i * 60;
            int tx = cx + (r + sz/12) * ((angle == 90 || angle == 270) ? 0 : 
                     ((angle < 180) ? 1 : -1)) * (90 - (angle % 90 > 45 ? 90 - angle % 90 : angle % 90)) / 90;
            int ty = cy + (r + sz/12) * ((angle == 0 || angle == 180) ? 0 :
                     ((angle < 90 || angle > 270) ? -1 : 1)) * (angle % 90 > 45 ? 90 - angle % 90 : angle % 90) / 90;
            nxr_fill_rect(tx - sz/16, ty - sz/16, sz/8, sz/8, color);
        }
        break;
    }
    
    case NXI_ICON_TERMINAL: {
        nxr_fill_rounded_rect(x + sz/8, y + sz/8, sz * 3/4, sz * 3/4, sz/10, 0xFF2D2D2D);
        for (int i = 0; i < sz/4; i++) {
            nxr_fill_rect(x + sz/4 + i, cy - i/2, 1, i + 1, color);
        }
        nxr_fill_rect(x + sz/2, cy + 2, sz/4, sz/12, color);
        break;
    }
    
    case NXI_ICON_FOLDER: {
        nxr_fill_rounded_rect(x + sz/8, y + sz/3, sz * 3/4, sz/2, sz/12, color);
        nxr_fill_rounded_rect(x + sz/8, y + sz/4, sz/3, sz/6, sz/16, color);
        break;
    }
    
    case NXI_ICON_SEARCH: {
        nxr_fill_circle(cx - sz/8, cy - sz/8, sz/4, color);
        nxr_fill_circle(cx - sz/8, cy - sz/8, sz/6, 0xFF1C1C1E);
        for (int i = 0; i < sz/4; i++) {
            nxr_fill_rect(cx + i, cy + i, sz/8, sz/8, color);
        }
        break;
    }
    
    default: {
        nxr_fill_circle(cx, cy, r, color);
        break;
    }
    }
}

/* ============ Bitmap Icon Blitting ============ */

static void nxi_blit_rgba(uint8_t *rgba, int img_w, int img_h,
                          int x, int y, int dest_w, int dest_h,
                          uint32_t tint_color) {
    /* Simple nearest-neighbor scaling with tint */
    uint8_t tint_r = (tint_color >> 16) & 0xFF;
    uint8_t tint_g = (tint_color >> 8) & 0xFF;
    uint8_t tint_b = tint_color & 0xFF;
    
    for (int dy = 0; dy < dest_h; dy++) {
        int sy = dy * img_h / dest_h;
        for (int dx = 0; dx < dest_w; dx++) {
            int sx = dx * img_w / dest_w;
            int src_idx = (sy * img_w + sx) * 4;
            
            uint8_t r = rgba[src_idx + 0];
            uint8_t g = rgba[src_idx + 1];
            uint8_t b = rgba[src_idx + 2];
            uint8_t a = rgba[src_idx + 3];
            
            if (a < 16) continue;  /* Skip nearly transparent */
            
            /* Apply tint (blend with tint color) */
            r = (r * tint_r) / 255;
            g = (g * tint_g) / 255;
            b = (b * tint_b) / 255;
            
            uint32_t color = (a << 24) | (r << 16) | (g << 8) | b;
            nxr_fill_rect(x + dx, y + dy, 1, 1, color);
        }
    }
}

/* ============ Public API ============ */

void nxi_render_init(void) {
    if (g_nxi_initialized) return;
    
    for (int i = 0; i < NXI_ICON_CACHE_SIZE; i++) {
        g_icon_cache[i].icon_id = 0;
        g_icon_cache[i].rgba = NULL;
        g_icon_cache[i].loaded = 0;
    }
    g_cache_count = 0;
    g_nxi_initialized = 1;
}

void nxi_render_shutdown(void) {
    for (int i = 0; i < g_cache_count; i++) {
        if (g_icon_cache[i].rgba) {
            nx_mfree(g_icon_cache[i].rgba);
            g_icon_cache[i].rgba = NULL;
        }
    }
    g_cache_count = 0;
    g_nxi_initialized = 0;
}

void nxi_render_icon(nx_context_t *ctx, uint32_t icon_id, 
                     int x, int y, int size, uint32_t tint_color) {
    (void)ctx;
    
    /* Check cache first */
    nxi_cached_icon_t *entry = nxi_cache_find(icon_id, size);
    
    if (!entry) {
        /* Allocate cache entry and try loading from file */
        entry = nxi_cache_alloc(icon_id, size);
        nxi_load_from_file(entry, icon_id, size);
    }
    
    if (entry->loaded == 1 && entry->rgba) {
        /* Use file-based icon */
        nxi_blit_rgba(entry->rgba, entry->actual_w, entry->actual_h,
                      x, y, size, size, tint_color);
    } else {
        /* Fall back to procedural icon */
        draw_builtin_icon(icon_id, x, y, size, tint_color);
    }
}

void nxi_render_icon_by_name(nx_context_t *ctx, const char *name,
                              int x, int y, int size, uint32_t tint_color) {
    uint32_t id = NXI_ICON_APP;
    
    if (!name) {
        nxi_render_icon(ctx, id, x, y, size, tint_color);
        return;
    }
    
    if (name[0] == 'w' && name[1] == 'i') id = NXI_ICON_WIFI;
    else if (name[0] == 'b' && name[1] == 'l') id = NXI_ICON_BLUETOOTH;
    else if (name[0] == 'e' && name[1] == 't') id = NXI_ICON_ETHERNET;
    else if (name[0] == 's' && name[1] == 'h') id = NXI_ICON_AIRDROP;
    else if (name[0] == 'f' && name[1] == 'o') id = (name[2] == 'c') ? NXI_ICON_FOCUS : NXI_ICON_FOLDER;
    else if (name[0] == 'd' && name[1] == 'a') id = NXI_ICON_DARK_MODE;
    else if (name[0] == 'm' && name[1] == 'i') id = NXI_ICON_MIRROR;
    else if (name[0] == 'k' && name[1] == 'e') id = NXI_ICON_KEYBOARD;
    else if (name[0] == 'b' && name[1] == 'r') id = NXI_ICON_BRIGHTNESS;
    else if (name[0] == 'v' && name[1] == 'o') id = NXI_ICON_VOLUME;
    else if (name[0] == 'p' && name[1] == 'l') id = NXI_ICON_PLAY;
    else if (name[0] == 'p' && name[1] == 'a') id = NXI_ICON_PAUSE;
    else if (name[0] == 'b' && name[1] == 'e') id = NXI_ICON_BELL;
    else if (name[0] == 'g' && name[1] == 'r') id = NXI_ICON_GRID;
    else if (name[0] == 's' && name[1] == 'e') id = (name[2] == 't') ? NXI_ICON_SETTINGS : NXI_ICON_SEARCH;
    else if (name[0] == 't' && name[1] == 'e') id = NXI_ICON_TERMINAL;
    else if (name[0] == 'c' && name[1] == 'l') id = NXI_ICON_CLOSE;
    
    nxi_render_icon(ctx, id, x, y, size, tint_color);
}

void nxi_draw_icon_fallback(uint32_t *fb, uint32_t pitch,
                            uint32_t icon_id, int x, int y, int size,
                            uint32_t tint_color) {
    (void)fb; (void)pitch;
    draw_builtin_icon(icon_id, x, y, size, tint_color);
}

void nxi_cache_clear(void) {
    for (int i = 0; i < g_cache_count; i++) {
        if (g_icon_cache[i].rgba) {
            nx_mfree(g_icon_cache[i].rgba);
            g_icon_cache[i].rgba = NULL;
        }
        g_icon_cache[i].loaded = 0;
    }
    g_cache_count = 0;
}
