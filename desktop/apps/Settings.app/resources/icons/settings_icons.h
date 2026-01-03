/*
 * NeolyxOS Settings - Icon Library
 * 
 * SVG path data for settings icons
 * Use with nxgfx or nxrender for rendering
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef SETTINGS_ICONS_H
#define SETTINGS_ICONS_H

/* Icon size constants */
#define ICON_SIZE_SMALL   16
#define ICON_SIZE_MEDIUM  24
#define ICON_SIZE_LARGE   32
#define ICON_SIZE_XLARGE  48

/* Icon identifiers */
typedef enum {
    /* Navigation */
    ICON_MONITOR = 0,
    ICON_SPEAKER,
    ICON_WIFI,
    ICON_BLUETOOTH,
    ICON_SHIELD,
    ICON_LOCK,
    ICON_CPU,
    ICON_ACTIVITY,
    ICON_ZAP,
    ICON_PALETTE,
    ICON_IMAGE,
    ICON_HARDDRIVE,
    ICON_TERMINAL,
    ICON_LAYERS,
    ICON_DOWNLOAD,
    ICON_BATTERY,
    ICON_CLOCK,
    ICON_CHART,
    ICON_BOXES,
    ICON_CODE,
    ICON_INFO,
    ICON_GEAR,
    ICON_USER,
    ICON_USERS,
    ICON_FOLDER,
    ICON_FILE,
    ICON_SEARCH,
    ICON_BELL,
    ICON_GLOBE,
    ICON_SUN,
    ICON_MOON,
    ICON_POWER,
    ICON_REFRESH,
    ICON_CHECK,
    ICON_X,
    ICON_ARROW_LEFT,
    ICON_ARROW_RIGHT,
    ICON_ARROW_UP,
    ICON_ARROW_DOWN,
    ICON_CHEVRON_RIGHT,
    ICON_PLUS,
    ICON_MINUS,
    ICON_EDIT,
    ICON_TRASH,
    ICON_EYE,
    ICON_EYE_OFF,
    ICON_VOLUME_HIGH,
    ICON_VOLUME_LOW,
    ICON_VOLUME_MUTE,
    ICON_MIC,
    ICON_MIC_OFF,
    ICON_CAMERA,
    ICON_PRINTER,
    ICON_KEYBOARD,
    ICON_MOUSE,
    ICON_GAMEPAD,
    ICON_HEADPHONES,
    ICON_WIFI_OFF,
    ICON_BLUETOOTH_OFF,
    ICON_AIRPLANE,
    ICON_ETHERNET,
    ICON_SERVER,
    ICON_DATABASE,
    ICON_CLOUD,
    ICON_CLOUD_DOWNLOAD,
    ICON_CLOUD_UPLOAD,
    ICON_KEY,
    ICON_FINGERPRINT,
    ICON_SCAN,
    ICON_EXTERNAL_LINK,
    ICON_LINK,
    ICON_COPY,
    ICON_CLIPBOARD,
    ICON_SAVE,
    ICON_UNDO,
    ICON_REDO,
    ICON_MAXIMIZE,
    ICON_MINIMIZE,
    ICON_FULLSCREEN,
    ICON_GRID,
    ICON_LIST,
    ICON_SLIDERS,
    ICON_TOGGLE_LEFT,
    ICON_TOGGLE_RIGHT,
    ICON_COUNT
} icon_id_t;

/* Icon path data - SVG paths for 24x24 viewport */
typedef struct {
    icon_id_t id;
    const char* name;
    const char* svg_path;
} icon_data_t;

/* Icon registry */
static const icon_data_t g_icons[] = {
    { ICON_MONITOR, "monitor", 
      "M4 3h16a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z M8 21h8 M12 17v4" },
    
    { ICON_SPEAKER, "speaker",
      "M11 5L6 9H2v6h4l5 4V5z M15.54 8.46a5 5 0 0 1 0 7.07 M19.07 4.93a10 10 0 0 1 0 14.14" },
    
    { ICON_WIFI, "wifi",
      "M5 12.55a11 11 0 0 1 14.08 0 M1.42 9a16 16 0 0 1 21.16 0 M8.53 16.11a6 6 0 0 1 6.95 0 M12 20h.01" },
    
    { ICON_BLUETOOTH, "bluetooth",
      "M6.5 6.5l11 11L12 23V1l5.5 5.5-11 11" },
    
    { ICON_SHIELD, "shield",
      "M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z" },
    
    { ICON_LOCK, "lock",
      "M19 11H5a2 2 0 0 0-2 2v7a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7a2 2 0 0 0-2-2z M7 11V7a5 5 0 0 1 10 0v4" },
    
    { ICON_CPU, "cpu",
      "M18 4H6a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V6a2 2 0 0 0-2-2z M9 9h6v6H9z M9 1v3 M15 1v3 M9 20v3 M15 20v3 M20 9h3 M20 14h3 M1 9h3 M1 14h3" },
    
    { ICON_ACTIVITY, "activity",
      "M22 12h-4l-3 9L9 3l-3 9H2" },
    
    { ICON_ZAP, "zap",
      "M13 2L3 14h9l-1 8 10-12h-9l1-8z" },
    
    { ICON_PALETTE, "palette",
      "M12 2a10 10 0 0 0-9.84 11.88A10 10 0 0 0 12 22a2 2 0 0 0 2-2v-1a2 2 0 0 1 2-2h1a10 10 0 0 0-5-15z M5.5 12a1 1 0 1 1 2 0 M9 8a1 1 0 1 1 2 0 M12.5 6a1 1 0 1 1 2 0 M16 8a1 1 0 1 1 2 0" },
    
    { ICON_IMAGE, "image",
      "M19 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V5a2 2 0 0 0-2-2z M8.5 10a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3z M21 15l-5-5L5 21" },
    
    { ICON_HARDDRIVE, "harddrive",
      "M22 12H2 M5.45 5.11L2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11z M6 16h.01 M10 16h.01" },
    
    { ICON_TERMINAL, "terminal",
      "M4 17l6-6-6-6 M12 19h8" },
    
    { ICON_LAYERS, "layers",
      "M12 2L2 7l10 5 10-5-10-5z M2 17l10 5 10-5 M2 12l10 5 10-5" },
    
    { ICON_DOWNLOAD, "download",
      "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4 M7 10l5 5 5-5 M12 15V3" },
    
    { ICON_BATTERY, "battery",
      "M17 6H3a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2z M23 13v-2 M5 10v4 M9 10v4" },
    
    { ICON_CLOCK, "clock",
      "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20z M12 6v6l4 2" },
    
    { ICON_CHART, "chart",
      "M18 20V10 M12 20V4 M6 20v-6" },
    
    { ICON_BOXES, "boxes",
      "M2.97 8.16A2 2 0 0 1 2 6.48V4.12C2 3.5 2.5 3 3.12 3h3.76a2 2 0 0 1 1.68.97l.64 1.03H17.12c.62 0 1.12.5 1.12 1.12v17.64c0 .62-.5 1.12-1.12 1.12H3.12A1.12 1.12 0 0 1 2 22.76V8.16z M10 15h4 M10 11h4" },
    
    { ICON_CODE, "code",
      "M16 18l6-6-6-6 M8 6L2 12l6 6" },
    
    { ICON_GEAR, "gear",
      "M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0z" },
    
    { ICON_USER, "user",
      "M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2 M12 3a4 4 0 1 0 0 8 4 4 0 0 0 0-8z" },
    
    { ICON_INFO, "info",
      "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20z M12 16v-4 M12 8h.01" },
    
    { ICON_SEARCH, "search",
      "M11 19a8 8 0 1 0 0-16 8 8 0 0 0 0 16z M21 21l-4.35-4.35" },
    
    { ICON_POWER, "power",
      "M18.36 6.64a9 9 0 1 1-12.73 0 M12 2v10" },
    
    { ICON_SUN, "sun",
      "M12 17a5 5 0 1 0 0-10 5 5 0 0 0 0 10z M12 1v2 M12 21v2 M4.22 4.22l1.42 1.42 M18.36 18.36l1.42 1.42 M1 12h2 M21 12h2 M4.22 19.78l1.42-1.42 M18.36 5.64l1.42-1.42" },
    
    { ICON_MOON, "moon",
      "M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z" },
    
    { ICON_CHECK, "check",
      "M20 6L9 17l-5-5" },
    
    { ICON_X, "x",
      "M18 6L6 18 M6 6l12 12" },
    
    { ICON_SLIDERS, "sliders",
      "M4 21v-7 M4 10V3 M12 21v-9 M12 8V3 M20 21v-5 M20 12V3 M1 14h6 M9 8h6 M17 16h6" },
    
    { ICON_TOGGLE_LEFT, "toggle-left",
      "M16 5H8a7 7 0 0 0 0 14h8a7 7 0 0 0 0-14z M8 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z" },
    
    { ICON_TOGGLE_RIGHT, "toggle-right",
      "M16 5H8a7 7 0 0 0 0 14h8a7 7 0 0 0 0-14z M16 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z" },
    
    { ICON_FINGERPRINT, "fingerprint",
      "M2 12a10 10 0 1 0 20 0 M12 2a10 10 0 0 1 10 10 M8 12a4 4 0 0 1 8 0 M12 8v8" },
    
    { ICON_KEY, "key",
      "M21 2l-2 2m-7.61 7.61a5.5 5.5 0 1 1-7.78 7.78 5.5 5.5 0 0 1 7.78-7.78zM15 9l6-6 M18 12l3-3" },
    
    { ICON_GLOBE, "globe",
      "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20z M2 12h20 M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z" },
    
    { ICON_CLOUD, "cloud",
      "M18 10h-1.26A8 8 0 1 0 9 20h9a5 5 0 0 0 0-10z" },
    
    { ICON_ETHERNET, "ethernet",
      "M5 12h14 M12 5v14 M5 12a7 7 0 1 0 14 0 7 7 0 0 0-14 0z" },
    
    { ICON_SERVER, "server",
      "M2 9h20 M2 15h20 M19 5H5a2 2 0 0 0-2 2v2a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V7a2 2 0 0 0-2-2z M19 13H5a2 2 0 0 0-2 2v2a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-2a2 2 0 0 0-2-2z M6 8h.01 M6 16h.01" },
};

/* Get icon by ID */
static inline const icon_data_t* get_icon(icon_id_t id) {
    if (id >= 0 && id < ICON_COUNT) {
        for (int i = 0; i < (int)(sizeof(g_icons)/sizeof(g_icons[0])); i++) {
            if (g_icons[i].id == id) return &g_icons[i];
        }
    }
    return NULL;
}

/* Get icon by name */
static inline const icon_data_t* get_icon_by_name(const char* name) {
    for (int i = 0; i < (int)(sizeof(g_icons)/sizeof(g_icons[0])); i++) {
        if (strcmp(g_icons[i].name, name) == 0) return &g_icons[i];
    }
    return NULL;
}

#endif /* SETTINGS_ICONS_H */
