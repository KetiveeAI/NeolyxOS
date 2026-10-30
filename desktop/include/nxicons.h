/*
 * NeolyxOS - NXIcons: the system vector icon set
 *
 * Original, resolution-independent symbols (menu bar, control center,
 * window controls) and full-color application icons, drawn with the
 * public NXCanvas API. Symbols are single-color line icons on a 24-unit
 * design grid with a 2-unit stroke, rounded caps and joins; they scale
 * to any size. App icons are continuous-curvature tiles with layered
 * gradients and a glyph; they are rendered once per size into an icon
 * cache and composited from there.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXICONS_H
#define NXICONS_H

#include <stdint.h>
#include "nxcanvas.h"

/* ============ Symbols ============ */

typedef enum {
    NXSYM_WIFI = 0,
    NXSYM_WIFI_OFF,
    NXSYM_ETHERNET,
    NXSYM_BLUETOOTH,
    NXSYM_BATTERY,
    NXSYM_BATTERY_CHARGING,
    NXSYM_VOLUME,
    NXSYM_VOLUME_MUTE,
    NXSYM_BRIGHTNESS,
    NXSYM_MOON,
    NXSYM_SUN,
    NXSYM_SEARCH,
    NXSYM_CONTROL_CENTER,
    NXSYM_BELL,
    NXSYM_GRID,
    NXSYM_CHEVRON_LEFT,
    NXSYM_CHEVRON_RIGHT,
    NXSYM_CHEVRON_UP,
    NXSYM_CHEVRON_DOWN,
    NXSYM_CLOSE,
    NXSYM_MINIMIZE,
    NXSYM_MAXIMIZE,
    NXSYM_PLUS,
    NXSYM_CHECK,
    NXSYM_GEAR,
    NXSYM_POWER,
    NXSYM_LOCK,
    NXSYM_UNLOCK,
    NXSYM_USER,
    NXSYM_KEYBOARD,
    NXSYM_AIRPLANE,
    NXSYM_SHIELD,
    NXSYM_PLAY,
    NXSYM_PAUSE,
    NXSYM_NEXT,
    NXSYM_PREV,
    NXSYM_FOLDER,
    NXSYM_DOCUMENT,
    NXSYM_TRASH,
    NXSYM_CLOCK,
    NXSYM_WARNING,
    NXSYM_INFO,
    NXSYM_MIC,
    NXSYM_COUNT
} nxsym_t;

/*
 * Draw a symbol centered at (cx, cy) in a size x size box (size is the
 * icon's design square; strokes scale with it). `value` in 0..1 drives
 * level icons: wifi bars, battery level, volume waves, brightness; it is
 * ignored by the others. Returns the first drawing error.
 */
nxc_status_t nxsym_draw(nxc_canvas_t *c, nxsym_t sym, float cx, float cy, float size,
                        nxc_color_t color, float value);

/* Diagnostic name ("wifi", ...), never NULL. */
const char *nxsym_name(nxsym_t sym);

/* ============ Application icons ============ */

typedef enum {
    NXAPP_FILES = 0,
    NXAPP_BROWSER,
    NXAPP_TERMINAL,
    NXAPP_SETTINGS,
    NXAPP_CALCULATOR,
    NXAPP_CALENDAR,
    NXAPP_CLOCK,
    NXAPP_NOTES,
    NXAPP_MUSIC,
    NXAPP_PHOTOS,
    NXAPP_MAIL,
    NXAPP_MESSAGES,
    NXAPP_FONTS,
    NXAPP_MONITOR,
    NXAPP_STORE,
    NXAPP_TRASH,
    NXAPP_LAUNCHER,
    NXAPP_GENERIC,
    /* Added after GENERIC so existing values keep their numbers. */
    NXAPP_REOLAB,
    NXAPP_ROLECUT,
    NXAPP_RIVEE,
    NXAPP_COUNT
} nxapp_icon_t;

/*
 * Initialize the icon cache (requires an initialized NXCanvas engine).
 * cache_bytes bounds the memory used for rendered app icons; 0 selects
 * 4 MiB. Without a cache, app icons render directly every time.
 * Also validates the linked icon pack (nxicon_pack.h); returns
 * NXC_ERR_UNSUPPORTED when any pack entry is damaged. Icons still draw
 * in that case, with the damaged ones replaced by fallbacks.
 */
nxc_status_t nxicons_init(uint32_t cache_bytes);

/*
 * Draw an app icon with its top-left corner at (x, y), `size` pixels
 * square, multiplied by alpha. Integer sizes are cached; fractional sizes
 * (dock magnification) are drawn by scaling the nearest cached size up
 * (never down) with smooth filtering.
 */
nxc_status_t nxapp_icon_draw(nxc_canvas_t *c, nxapp_icon_t id, float x, float y, float size,
                             uint8_t alpha);

/* Map an application name ("Files", "terminal", ...) to its icon;
 * unknown names give NXAPP_GENERIC. */
nxapp_icon_t nxapp_icon_from_name(const char *app_name);

/* Diagnostic name ("files", ...), never NULL. */
const char *nxapp_icon_name(nxapp_icon_t id);

#endif /* NXICONS_H */
