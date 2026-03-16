/*
 * NeolyxOS - NXI Icon Renderer using NXRender
 * 
 * Renders NXI vector icons using NXRender graphics primitives
 * for smooth, anti-aliased output.
 * 
 * Copyright (c) 2025 KetiveeAI
 * 
 * ============================================================================
 * !!! MANDATORY ICON POLICY - READ BEFORE ADDING ANY ICON CODE !!!
 * ============================================================================
 * 
 * ALL ICONS IN NEOLYXOS MUST BE LOADED FROM .NXI FILES.
 * 
 * DO NOT:
 * - Draw icon shapes using primitives (fill_rect, fill_circle, draw_line)
 * - Create hardcoded icon arrays or bitmaps in code
 * - Add "temporary" placeholder icon drawing functions
 * 
 * INSTEAD:
 * - Create .nxi icon files using IconLay.app
 * - Store icons in /System/Icons/ or /Applications/[App].app/resources/icons/
 * - Use nxi_render_icon() or nxi_draw_icon_fallback() to render
 * 
 * If you need a new icon:
 * 1. Open IconLay.app
 * 2. Design the icon
 * 3. Export as .nxi to appropriate location
 * 4. Add icon ID to this header if system-wide
 * 
 * VIOLATION OF THIS POLICY WILL BE REJECTED IN CODE REVIEW.
 * ============================================================================
 */

#ifndef NXICON_RENDER_H
#define NXICON_RENDER_H

#include <stdint.h>
#include <stddef.h>

/* Forward declaration */
struct nx_context;
typedef struct nx_context nx_context_t;

/* Icon cache entry */
typedef struct {
    uint32_t icon_id;
    int size;
    uint8_t *rgba_data;  /* Cached rasterized RGBA */
} nxi_cache_entry_t;

/* Icon cache */
#define NXI_CACHE_SIZE 32
typedef struct {
    nxi_cache_entry_t entries[NXI_CACHE_SIZE];
    int count;
} nxi_cache_t;

/* Initialize the icon renderer */
void nxi_render_init(void);

/* Shutdown and free cache */
void nxi_render_shutdown(void);

/* Render icon by ID using NXRender context */
void nxi_render_icon(nx_context_t *ctx, uint32_t icon_id, 
                     int x, int y, int size, uint32_t tint_color);

/* Render icon by name */
void nxi_render_icon_by_name(nx_context_t *ctx, const char *name,
                              int x, int y, int size, uint32_t tint_color);

/* Draw icon directly to framebuffer using fallback (no context) */
void nxi_draw_icon_fallback(uint32_t *fb, uint32_t pitch,
                            uint32_t icon_id, int x, int y, int size,
                            uint32_t tint_color);

/* Clear icon cache (call when theme changes) */
void nxi_cache_clear(void);

/* ============ Built-in System Icons ============ */

/* Pre-defined icon IDs matching kernel nxicon.h */
#define NXI_ICON_APP          1
#define NXI_ICON_FOLDER       2
#define NXI_ICON_FILE         4
#define NXI_ICON_TERMINAL     9
#define NXI_ICON_SETTINGS     10
#define NXI_ICON_SEARCH       16
#define NXI_ICON_CLOSE        20
#define NXI_ICON_MINIMIZE     21
#define NXI_ICON_MAXIMIZE     22
#define NXI_ICON_WIFI         100
#define NXI_ICON_BLUETOOTH    101
#define NXI_ICON_AIRDROP      102
#define NXI_ICON_ETHERNET     103
#define NXI_ICON_BRIGHTNESS   104
#define NXI_ICON_VOLUME       105
#define NXI_ICON_FOCUS        106
#define NXI_ICON_MIRROR       107
#define NXI_ICON_DARK_MODE    108
#define NXI_ICON_KEYBOARD     109
#define NXI_ICON_PLAY         110
#define NXI_ICON_PAUSE        111
#define NXI_ICON_PREV         112
#define NXI_ICON_NEXT         113
#define NXI_ICON_BELL         114
#define NXI_ICON_GRID         115

/* Security Icons */
#define NXI_ICON_SHIELD       120
#define NXI_ICON_SHIELD_CHECK 121
#define NXI_ICON_LOCK         122
#define NXI_ICON_UNLOCK       123
#define NXI_ICON_FIREWALL     124
#define NXI_ICON_WARNING      125
#define NXI_ICON_NETWORK      126
#define NXI_ICON_NETWORK_OFF  127

#endif /* NXICON_RENDER_H */
