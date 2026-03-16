/*
 * NeolyxOS Settings Application - Main Entry Point
 * 
 * macOS-inspired Settings with advanced tab system:
 * - Multiple tabs can be open simultaneously
 * - Split view: 2-3 panels side by side
 * - Pop-out tabs to separate windows
 * - Resizable dividers
 * - Global search across all panels
 * - Hover preview on tabs
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings.h"
#include "../../include/control_center.h"  /* For pending panel from Control Center */
#include <string.h>

/* ============================================================================
 * Tab System Constants
 * ============================================================================ */

#define MAX_OPEN_TABS      8      /* Maximum tabs open at once */
#define MAX_SPLIT_VIEWS    3      /* Maximum panels in split view */
#define MIN_PANEL_WIDTH    280    /* Minimum width for a panel */
#define SIDEBAR_WIDTH      220    /* Left menu sidebar width */
#define TAB_HEIGHT         36     /* Tab bar height */
#define DIVIDER_WIDTH      6      /* Resizable divider width */

/* ============================================================================
 * Tab and View Structures
 * ============================================================================ */

typedef struct {
    panel_id_t panel;            /* Which panel this tab shows */
    int active;                  /* Is this tab active in split view */
    int popped_out;              /* Is this a popped-out window */
    float width_ratio;           /* Width ratio in split view (0.0-1.0) */
    rx_window* popup_window;     /* Popup window if popped out */
} settings_tab_t;

typedef struct {
    settings_tab_t tabs[MAX_OPEN_TABS];
    int tab_count;               /* Number of open tabs */
    int active_tabs[MAX_SPLIT_VIEWS];  /* Indices of visible split tabs */
    int split_count;             /* How many panels in split view (1-3) */
    int hovered_tab;             /* Tab being hovered for preview */
    int dragging_divider;        /* Which divider is being dragged (-1 = none) */
    int auto_split;              /* Auto-split when opening new tab */
} tab_state_t;

/* Global state */
static settings_ctx_t g_settings;
static tab_state_t g_tabs;

/* ============================================================================
 * Panel Data
 * ============================================================================ */

static const char* panel_names[PANEL_COUNT] = {
    "System",
    "Network",
    "Bluetooth",
    "Display",
    "Sound",
    "Storage",
    "Accounts",
    "Security",
    "Privacy",
    "Appearance",
    "Power",
    "Processes",
    "Startup Apps",
    "Scheduler",
    "Extensions",
    "System Updates",
    "Bootloader",
    "Developer",
    "About",
    "Keyboard",
    "Trackpad & Touch",
    "Color Management",
    "Apps",
    "Display Manager",
    "Devices",
    "Icons",
    "Windows",
    "Mouse",
    "Date & Time"
};

static const char* panel_icons[PANEL_COUNT] = {
    "gear", "wifi", "bluetooth", "display", "speaker",
    "harddisk", "person", "shield", "lock", "paintbrush",
    "battery", "chart.bar", "rocket", "clock", "puzzle",
    "arrow.down.circle", "terminal", "hammer", "info.circle", "keyboard",
    "hand.tap", "palette", "app.grid", "display.2", "cpu",
    "photo", "window", "mouse", "clock.fill"
};

/* Panel descriptions for search and hover preview */
static const char* panel_descriptions[PANEL_COUNT] = {
    "Display, sound, notifications, and system info",
    "WiFi, ethernet, VPN, and proxy settings",
    "Bluetooth devices and pairing",
    "Brightness, resolution, Night Shift, and scale",
    "Volume, output devices, and sound effects",
    "Disk space, drives, and storage optimization",
    "User accounts, login, and passwords",
    "Firewall, FileVault, and app security",
    "Location, camera, microphone, and analytics",
    "Theme, accent colors, fonts, and icons",
    "Battery, sleep, energy saver, and performance",
    "Running processes, CPU, and memory usage",
    "Apps that launch at login",
    "Scheduled tasks and automation",
    "System extensions and plugins",
    "macOS and app updates",
    "Startup disk and boot options",
    "Developer tools and debugging",
    "Software version, serial number, and license",
    "Keyboard layout, shortcuts, and NX key",
    "Multi-touch gestures and trackpad settings",
    "Display color profiles and calibration",
    "Installed applications and defaults",
    "External displays and arrangement",
    "Connected hardware devices and drivers",
    "Icon packs and customization",
    "Window behavior and snap settings",
    "Mouse speed, buttons, and scrolling",
    "Timezone, date format, and clock settings"
};

/* ============================================================================
 * Search Implementation
 * ============================================================================ */

static const char* system_keywords[] = { "system", "computer", "name", "hostname", "os", "restart", "shutdown" };
static const char* network_keywords[] = { "network", "wifi", "ethernet", "ip", "internet", "dns", "proxy", "vpn" };
static const char* bluetooth_keywords[] = { "bluetooth", "device", "pair", "connect", "wireless", "headphone" };
static const char* display_keywords[] = { "display", "screen", "resolution", "brightness", "night", "scale" };
static const char* sound_keywords[] = { "sound", "audio", "volume", "speaker", "microphone", "output", "input" };
static const char* storage_keywords[] = { "storage", "disk", "space", "drive", "ssd", "hdd", "files", "trash" };
static const char* accounts_keywords[] = { "accounts", "user", "login", "password", "profile", "avatar" };
static const char* security_keywords[] = { "security", "firewall", "encryption", "password", "lock" };
static const char* privacy_keywords[] = { "privacy", "location", "camera", "microphone", "tracking", "analytics" };
static const char* appearance_keywords[] = { "appearance", "theme", "dark", "light", "color", "font", "icon", "cursor" };
static const char* power_keywords[] = { "power", "battery", "sleep", "energy", "performance", "saver" };
static const char* processes_keywords[] = { "process", "task", "cpu", "memory", "usage", "monitor", "kill" };
static const char* startup_keywords[] = { "startup", "boot", "launch", "autostart", "app" };
static const char* scheduler_keywords[] = { "scheduler", "cron", "task", "job", "time", "plan" };
static const char* extensions_keywords[] = { "extension", "plugin", "addon", "widget", "snapping" };
static const char* updates_keywords[] = { "updates", "software", "upgrade", "download", "install", "version" };
static const char* bootloader_keywords[] = { "boot", "loader", "uefi", "kernel", "entry", "menu" };
static const char* developer_keywords[] = { "developer", "debug", "kernel", "module", "log", "usb" };
static const char* about_keywords[] = { "about", "system", "info", "version", "license", "hardware" };
static const char* keyboard_keywords[] = { "keyboard", "layout", "input", "keys", "shortcuts", "nx", "qwerty" };
static const char* trackpad_keywords[] = { "trackpad", "touch", "gesture", "scroll", "swipe", "pinch", "tap" };
static const char* color_keywords[] = { "color", "calibration", "profile", "icc", "display", "gamut", "srgb" };
static const char* apps_keywords[] = { "apps", "applications", "default", "open", "install", "uninstall", "permissions" };
static const char* displaymgr_keywords[] = { "display", "monitor", "external", "arrangement", "mirror", "extend" };
static const char* devices_keywords[] = { "devices", "hardware", "driver", "usb", "pci", "connected", "peripheral" };

void settings_search_init(settings_ctx_t* ctx) {
    memset(&ctx->search, 0, sizeof(search_ctx_t));
}

void settings_search_query(settings_ctx_t* ctx, const char* query) {
    if (!query || strlen(query) < 2) {
        settings_search_clear(ctx);
        return;
    }
    
    strncpy(ctx->search.query, query, 255);
    ctx->search.result_count = 0;
    ctx->search.active = true;
    
    const char** keyword_lists[PANEL_COUNT] = {
        system_keywords, network_keywords, bluetooth_keywords, display_keywords, sound_keywords,
        storage_keywords, accounts_keywords, security_keywords, privacy_keywords, appearance_keywords,
        power_keywords, processes_keywords, startup_keywords, scheduler_keywords, extensions_keywords,
        updates_keywords, bootloader_keywords, developer_keywords, about_keywords, keyboard_keywords,
        trackpad_keywords, color_keywords, apps_keywords, displaymgr_keywords, devices_keywords
    };
    int keyword_counts[PANEL_COUNT] = { 7, 8, 6, 6, 7, 8, 6, 5, 6, 8, 6, 7, 5, 6, 5, 6, 6, 6, 6, 7, 7, 7, 7, 6, 7 };
    
    for (int p = 0; p < PANEL_COUNT && ctx->search.result_count < MAX_SEARCH_RESULTS; p++) {
        int score = 0;
        if (strcasestr(panel_names[p], query)) score += 20;
        for (int k = 0; k < keyword_counts[p]; k++) {
            if (strcasestr(keyword_lists[p][k], query) || strcasestr(query, keyword_lists[p][k])) {
                score += 10;
            }
        }
        if (score > 0) {
            search_result_t* result = &ctx->search.results[ctx->search.result_count++];
            result->title = panel_names[p];
            result->description = panel_descriptions[p];
            result->panel = (panel_id_t)p;
            result->sub_page = NULL;
            result->score = score;
        }
    }
}

void settings_search_clear(settings_ctx_t* ctx) {
    memset(&ctx->search, 0, sizeof(search_ctx_t));
}

/* ============================================================================
 * Tab Management
 * ============================================================================ */

static void tabs_init(void) {
    memset(&g_tabs, 0, sizeof(tab_state_t));
    g_tabs.hovered_tab = -1;
    g_tabs.dragging_divider = -1;
    g_tabs.auto_split = 0;  /* Default: full screen for new tabs */
    g_tabs.split_count = 1;
    
    /* Check if Control Center requested a specific panel */
    int pending_panel = control_center_get_pending_panel();
    panel_id_t initial_panel = PANEL_SYSTEM;
    if (pending_panel > 0 && pending_panel < PANEL_COUNT) {
        initial_panel = (panel_id_t)pending_panel;
        println("[Settings] Opening panel from Control Center:");
        println(panel_names[initial_panel]);
    }
    
    /* Open initial panel */
    g_tabs.tabs[0].panel = initial_panel;
    g_tabs.tabs[0].active = 1;
    g_tabs.tabs[0].width_ratio = 1.0f;
    g_tabs.tab_count = 1;
    g_tabs.active_tabs[0] = 0;
}

/* Open a new tab (or focus existing) */
static int tab_open(panel_id_t panel) {
    /* Check if already open */
    for (int i = 0; i < g_tabs.tab_count; i++) {
        if (g_tabs.tabs[i].panel == panel && !g_tabs.tabs[i].popped_out) {
            /* Already open - make it active in split view */
            if (g_tabs.split_count < MAX_SPLIT_VIEWS && g_tabs.auto_split) {
                g_tabs.active_tabs[g_tabs.split_count] = i;
                g_tabs.split_count++;
                /* Redistribute widths */
                float width = 1.0f / g_tabs.split_count;
                for (int j = 0; j < g_tabs.split_count; j++) {
                    g_tabs.tabs[g_tabs.active_tabs[j]].width_ratio = width;
                }
            } else {
                /* Replace first active tab */
                g_tabs.active_tabs[0] = i;
            }
            return i;
        }
    }
    
    /* Add new tab */
    if (g_tabs.tab_count >= MAX_OPEN_TABS) {
        println("[Settings] Maximum tabs reached");
        return -1;
    }
    
    int idx = g_tabs.tab_count++;
    g_tabs.tabs[idx].panel = panel;
    g_tabs.tabs[idx].active = 1;
    g_tabs.tabs[idx].popped_out = 0;
    g_tabs.tabs[idx].popup_window = NULL;
    
    /* Handle split view */
    if (g_tabs.split_count < MAX_SPLIT_VIEWS && g_tabs.auto_split) {
        g_tabs.active_tabs[g_tabs.split_count] = idx;
        g_tabs.split_count++;
        /* Redistribute widths equally */
        float width = 1.0f / g_tabs.split_count;
        for (int j = 0; j < g_tabs.split_count; j++) {
            g_tabs.tabs[g_tabs.active_tabs[j]].width_ratio = width;
        }
    } else {
        /* Replace first active */
        g_tabs.tabs[idx].width_ratio = 1.0f;
        g_tabs.active_tabs[0] = idx;
        g_tabs.split_count = 1;
    }
    
    println("[Settings] Opened tab:");
    println(panel_names[panel]);
    return idx;
}

/* Close a tab */
static void tab_close(int tab_idx) {
    if (tab_idx < 0 || tab_idx >= g_tabs.tab_count) return;
    
    /* If popped out, close the window */
    if (g_tabs.tabs[tab_idx].popped_out && g_tabs.tabs[tab_idx].popup_window) {
        /* Would call window_close() here */
        g_tabs.tabs[tab_idx].popup_window = NULL;
    }
    
    /* Remove from active tabs */
    for (int i = 0; i < g_tabs.split_count; i++) {
        if (g_tabs.active_tabs[i] == tab_idx) {
            /* Shift remaining */
            for (int j = i; j < g_tabs.split_count - 1; j++) {
                g_tabs.active_tabs[j] = g_tabs.active_tabs[j + 1];
            }
            g_tabs.split_count--;
            break;
        }
    }
    
    /* Remove from tabs array */
    for (int i = tab_idx; i < g_tabs.tab_count - 1; i++) {
        g_tabs.tabs[i] = g_tabs.tabs[i + 1];
    }
    g_tabs.tab_count--;
    
    /* Update active tab indices */
    for (int i = 0; i < g_tabs.split_count; i++) {
        if (g_tabs.active_tabs[i] > tab_idx) {
            g_tabs.active_tabs[i]--;
        }
    }
    
    /* Ensure at least one tab */
    if (g_tabs.tab_count == 0) {
        tab_open(PANEL_SYSTEM);
    }
    
    /* Redistribute widths */
    if (g_tabs.split_count > 0) {
        float width = 1.0f / g_tabs.split_count;
        for (int i = 0; i < g_tabs.split_count; i++) {
            g_tabs.tabs[g_tabs.active_tabs[i]].width_ratio = width;
        }
    }
}

/* Pop out a tab to its own window */
static void tab_pop_out(int tab_idx) {
    if (tab_idx < 0 || tab_idx >= g_tabs.tab_count) return;
    if (g_tabs.tabs[tab_idx].popped_out) return;
    
    g_tabs.tabs[tab_idx].popped_out = 1;
    
    /* Create new window for this panel */
    char title[64];
    snprintf(title, sizeof(title), "Settings - %s", panel_names[g_tabs.tabs[tab_idx].panel]);
    
    /* Would create window here */
    /* g_tabs.tabs[tab_idx].popup_window = app_create_window(g_settings.app, title, 600, 500); */
    
    /* Remove from split view */
    for (int i = 0; i < g_tabs.split_count; i++) {
        if (g_tabs.active_tabs[i] == tab_idx) {
            for (int j = i; j < g_tabs.split_count - 1; j++) {
                g_tabs.active_tabs[j] = g_tabs.active_tabs[j + 1];
            }
            g_tabs.split_count--;
            break;
        }
    }
    
    /* If no active tabs left, activate first non-popped tab */
    if (g_tabs.split_count == 0) {
        for (int i = 0; i < g_tabs.tab_count; i++) {
            if (!g_tabs.tabs[i].popped_out) {
                g_tabs.active_tabs[0] = i;
                g_tabs.split_count = 1;
                g_tabs.tabs[i].width_ratio = 1.0f;
                break;
            }
        }
    }
    
    println("[Settings] Popped out tab to window");
}

/* Pop in a window back to tab bar */
static void tab_pop_in(int tab_idx) {
    if (tab_idx < 0 || tab_idx >= g_tabs.tab_count) return;
    if (!g_tabs.tabs[tab_idx].popped_out) return;
    
    g_tabs.tabs[tab_idx].popped_out = 0;
    
    /* Close popup window */
    /* Would call window_close() here */
    g_tabs.tabs[tab_idx].popup_window = NULL;
    
    /* Add back to split view */
    if (g_tabs.split_count < MAX_SPLIT_VIEWS) {
        g_tabs.active_tabs[g_tabs.split_count] = tab_idx;
        g_tabs.split_count++;
        /* Redistribute widths */
        float width = 1.0f / g_tabs.split_count;
        for (int i = 0; i < g_tabs.split_count; i++) {
            g_tabs.tabs[g_tabs.active_tabs[i]].width_ratio = width;
        }
    }
    
    println("[Settings] Popped in tab from window");
}

/* Toggle split view mode */
static void toggle_split_view(void) {
    if (g_tabs.split_count >= MAX_SPLIT_VIEWS) {
        /* Collapse to single */
        g_tabs.split_count = 1;
        g_tabs.tabs[g_tabs.active_tabs[0]].width_ratio = 1.0f;
    } else if (g_tabs.tab_count > g_tabs.split_count) {
        /* Add next available tab to split */
        for (int i = 0; i < g_tabs.tab_count; i++) {
            int already_active = 0;
            for (int j = 0; j < g_tabs.split_count; j++) {
                if (g_tabs.active_tabs[j] == i) {
                    already_active = 1;
                    break;
                }
            }
            if (!already_active && !g_tabs.tabs[i].popped_out) {
                g_tabs.active_tabs[g_tabs.split_count] = i;
                g_tabs.split_count++;
                break;
            }
        }
        /* Redistribute widths */
        float width = 1.0f / g_tabs.split_count;
        for (int i = 0; i < g_tabs.split_count; i++) {
            g_tabs.tabs[g_tabs.active_tabs[i]].width_ratio = width;
        }
    }
}

/* ============================================================================
 * UI Creation
 * ============================================================================ */

/* Forward declarations */
static rx_view* create_sidebar(void);
static rx_view* create_tab_bar(void);
static rx_view* create_split_content(void);
static rx_view* create_panel_view(panel_id_t panel, float width_ratio);
static rx_view* create_divider(int divider_idx);
static rx_view* create_search_bar(void);
static rx_view* create_hover_preview(int tab_idx);

/* Create global search bar */
static rx_view* create_search_bar(void) {
    rx_view* search_container = hstack_new(8.0f);
    if (!search_container) return NULL;
    
    search_container->box.background = (rx_color){ 44, 44, 46, 255 };
    search_container->box.padding = insets_all(10.0f);
    search_container->box.corner_radius = corners_all(8.0f);
    
    /* Search icon */
    rx_text_view* icon = text_view_new("Search all settings...");
    if (icon) {
        icon->color = NX_COLOR_TEXT_DIM;
        text_view_set_font_size(icon, 13.0f);
        view_add_child(search_container, (rx_view*)icon);
    }
    
    return search_container;
}

/* Create sidebar menu (always visible on left) */
static rx_view* create_sidebar(void) {
    rx_view* sidebar = vstack_new(2.0f);
    if (!sidebar) return NULL;
    
    sidebar->box.background = (rx_color){ 30, 30, 32, 255 };
    sidebar->box.padding = insets_all(8.0f);
    sidebar->box.width = SIDEBAR_WIDTH;
    
    /* Search bar at top */
    rx_view* search = create_search_bar();
    if (search) view_add_child(sidebar, search);
    
    /* Spacer */
    rx_view* spacer1 = vstack_new(0);
    if (spacer1) { spacer1->box.height = 16; view_add_child(sidebar, spacer1); }
    
    /* Panel list */
    for (int i = 0; i < PANEL_COUNT; i++) {
        rx_button_view* btn = button_view_new(panel_names[i]);
        if (!btn) continue;
        
        /* Check if this panel is open in any tab */
        int is_open = 0;
        for (int t = 0; t < g_tabs.tab_count; t++) {
            if (g_tabs.tabs[t].panel == (panel_id_t)i) {
                is_open = 1;
                break;
            }
        }
        
        if (is_open) {
            btn->normal_color = NX_COLOR_PRIMARY;
        } else {
            btn->normal_color = (rx_color){ 50, 50, 52, 255 };
        }
        btn->hover_color = (rx_color){ 70, 70, 72, 255 };
        btn->pressed_color = (rx_color){ 40, 40, 42, 255 };
        
        view_add_child(sidebar, (rx_view*)btn);
    }
    
    /* Spacer */
    rx_view* spacer2 = vstack_new(0);
    if (spacer2) { spacer2->box.height = 16; view_add_child(sidebar, spacer2); }
    
    /* Split view toggle */
    rx_button_view* split_btn = button_view_new(g_tabs.auto_split ? "Auto-Split: ON" : "Auto-Split: OFF");
    if (split_btn) {
        split_btn->normal_color = g_tabs.auto_split ? (rx_color){ 52, 199, 89, 255 } : (rx_color){ 60, 60, 62, 255 };
        split_btn->hover_color = (rx_color){ 80, 80, 82, 255 };
        view_add_child(sidebar, (rx_view*)split_btn);
    }
    
    return sidebar;
}

/* Create tab bar showing all open tabs */
static rx_view* create_tab_bar(void) {
    rx_view* tab_bar = hstack_new(4.0f);
    if (!tab_bar) return NULL;
    
    tab_bar->box.background = (rx_color){ 38, 38, 40, 255 };
    tab_bar->box.padding = (rx_insets){ 4, 8, 4, 8 };
    tab_bar->box.height = TAB_HEIGHT;
    
    /* Create a tab for each open panel */
    for (int i = 0; i < g_tabs.tab_count; i++) {
        if (g_tabs.tabs[i].popped_out) continue;  /* Skip popped-out tabs */
        
        rx_view* tab = hstack_new(6.0f);
        if (!tab) continue;
        
        /* Check if this tab is active in split view */
        int is_active = 0;
        for (int j = 0; j < g_tabs.split_count; j++) {
            if (g_tabs.active_tabs[j] == i) {
                is_active = 1;
                break;
            }
        }
        
        tab->box.background = is_active ? NX_COLOR_PRIMARY : (rx_color){ 55, 55, 57, 255 };
        tab->box.padding = (rx_insets){ 6, 12, 6, 12 };
        tab->box.corner_radius = corners_all(6.0f);
        
        /* Tab name */
        rx_text_view* name = text_view_new(panel_names[g_tabs.tabs[i].panel]);
        if (name) {
            text_view_set_font_size(name, 12.0f);
            name->color = (rx_color){ 255, 255, 255, 255 };
            view_add_child(tab, (rx_view*)name);
        }
        
        /* Close button */
        rx_text_view* close_btn = text_view_new("×");
        if (close_btn) {
            text_view_set_font_size(close_btn, 14.0f);
            close_btn->color = (rx_color){ 200, 200, 200, 255 };
            view_add_child(tab, (rx_view*)close_btn);
        }
        
        /* Pop-out button */
        rx_text_view* popout_btn = text_view_new("⇗");
        if (popout_btn) {
            text_view_set_font_size(popout_btn, 12.0f);
            popout_btn->color = (rx_color){ 180, 180, 180, 255 };
            view_add_child(tab, (rx_view*)popout_btn);
        }
        
        view_add_child(tab_bar, tab);
    }
    
    /* Add new tab button */
    rx_button_view* new_tab = button_view_new("+");
    if (new_tab) {
        new_tab->normal_color = (rx_color){ 50, 50, 52, 255 };
        new_tab->hover_color = (rx_color){ 70, 70, 72, 255 };
        view_add_child(tab_bar, (rx_view*)new_tab);
    }
    
    /* Split toggle button */
    char split_label[16];
    snprintf(split_label, sizeof(split_label), "Split %d", g_tabs.split_count);
    rx_button_view* split_toggle = button_view_new(split_label);
    if (split_toggle) {
        split_toggle->normal_color = (rx_color){ 50, 50, 52, 255 };
        split_toggle->hover_color = (rx_color){ 70, 70, 72, 255 };
        view_add_child(tab_bar, (rx_view*)split_toggle);
    }
    
    return tab_bar;
}

/* Create resizable divider between split panels */
/* Features:
 * - Hover capsule in center with expand/resize controls
 * - Directional arrows that show based on drag direction
 * - Full-screen expand button
 * - Visual feedback for resize direction
 */
static rx_view* create_divider(int divider_idx) {
    rx_view* divider = vstack_new(0);
    if (!divider) return NULL;
    
    /* Main divider bar - subtle when not hovered */
    divider->box.background = (rx_color){ 50, 50, 52, 255 };
    divider->box.width = DIVIDER_WIDTH;
    divider->box.min_height = 100;  /* Ensure minimum height */
    
    /* Spacer to push capsule to vertical center */
    rx_view* top_spacer = vstack_new(0);
    if (top_spacer) {
        top_spacer->box.flex_grow = 1.0f;
        view_add_child(divider, top_spacer);
    }
    
    /* ═══════════════════════════════════════════════════════════════════════
     * HOVER CAPSULE - Appears in center when mouse hovers over divider
     * Contains: Left Arrow | Expand Full | Right Arrow
     * ═══════════════════════════════════════════════════════════════════════ */
    rx_view* capsule = vstack_new(4.0f);
    if (capsule) {
        /* Capsule styling - pill shape with glassmorphism */
        capsule->box.background = (rx_color){ 40, 40, 42, 230 };
        capsule->box.padding = (rx_insets){ 8, 6, 8, 6 };
        capsule->box.corner_radius = corners_all(16.0f);
        capsule->box.width = 40;
        
        /* ─────────── EXPAND LEFT ARROW ─────────── */
        /* Shows when dragging right (expanding left panel) */
        rx_view* left_arrow_container = hstack_new(0);
        if (left_arrow_container) {
            left_arrow_container->box.padding = (rx_insets){ 4, 8, 4, 8 };
            left_arrow_container->box.corner_radius = corners_all(4.0f);
            left_arrow_container->box.background = (rx_color){ 60, 60, 62, 200 };
            
            /* Left arrow: ◀ or ← */
            rx_text_view* left_arrow = text_view_new("◀");
            if (left_arrow) {
                text_view_set_font_size(left_arrow, 14.0f);
                left_arrow->color = (rx_color){ 180, 180, 182, 255 };
                /* In runtime: color changes to blue when active */
                /* left_arrow->color = NX_COLOR_PRIMARY; when dragging left */
                view_add_child(left_arrow_container, (rx_view*)left_arrow);
            }
            
            view_add_child(capsule, left_arrow_container);
        }
        
        /* ─────────── EXPAND TO FULL SCREEN BUTTON ─────────── */
        rx_view* expand_btn = vstack_new(0);
        if (expand_btn) {
            expand_btn->box.background = NX_COLOR_PRIMARY;
            expand_btn->box.padding = (rx_insets){ 6, 8, 6, 8 };
            expand_btn->box.corner_radius = corners_all(6.0f);
            
            /* Expand icon: ⤢ or ⛶ or ⊞ */
            rx_text_view* expand_icon = text_view_new("⊞");
            if (expand_icon) {
                text_view_set_font_size(expand_icon, 12.0f);
                expand_icon->color = (rx_color){ 255, 255, 255, 255 };
                view_add_child(expand_btn, (rx_view*)expand_icon);
            }
            
            view_add_child(capsule, expand_btn);
        }
        
        /* ─────────── EXPAND RIGHT ARROW ─────────── */
        /* Shows when dragging left (expanding right panel) */
        rx_view* right_arrow_container = hstack_new(0);
        if (right_arrow_container) {
            right_arrow_container->box.padding = (rx_insets){ 4, 8, 4, 8 };
            right_arrow_container->box.corner_radius = corners_all(4.0f);
            right_arrow_container->box.background = (rx_color){ 60, 60, 62, 200 };
            
            /* Right arrow: ▶ or → */
            rx_text_view* right_arrow = text_view_new("▶");
            if (right_arrow) {
                text_view_set_font_size(right_arrow, 14.0f);
                right_arrow->color = (rx_color){ 180, 180, 182, 255 };
                /* In runtime: color changes to blue when active */
                view_add_child(right_arrow_container, (rx_view*)right_arrow);
            }
            
            view_add_child(capsule, right_arrow_container);
        }
        
        /* ─────────── RESIZE GRIP DOTS ─────────── */
        rx_view* grip = vstack_new(3.0f);
        if (grip) {
            grip->box.padding = (rx_insets){ 4, 0, 4, 0 };
            
            for (int i = 0; i < 3; i++) {
                rx_view* dot = vstack_new(0);
                if (dot) {
                    dot->box.background = (rx_color){ 120, 120, 122, 255 };
                    dot->box.width = 4;
                    dot->box.height = 4;
                    dot->box.corner_radius = corners_all(2.0f);
                    view_add_child(grip, dot);
                }
            }
            view_add_child(capsule, grip);
        }
        
        view_add_child(divider, capsule);
    }
    
    /* Bottom spacer to keep capsule centered */
    rx_view* bottom_spacer = vstack_new(0);
    if (bottom_spacer) {
        bottom_spacer->box.flex_grow = 1.0f;
        view_add_child(divider, bottom_spacer);
    }
    
    /* ═══════════════════════════════════════════════════════════════════════
     * DIVIDER INTERACTION NOTES (for runtime implementation):
     * 
     * 1. DEFAULT STATE:
     *    - Thin line (6px), subtle gray
     *    - Capsule hidden (opacity 0)
     * 
     * 2. HOVER STATE:
     *    - Divider highlights slightly
     *    - Capsule fades in (opacity 1)
     *    - Both arrows visible, neutral color
     * 
     * 3. DRAGGING LEFT (expanding right panel):
     *    - Left arrow DISAPPEARS
     *    - Right arrow turns BLUE and pulses
     *    - Capsule follows cursor vertically
     * 
     * 4. DRAGGING RIGHT (expanding left panel):
     *    - Right arrow DISAPPEARS
     *    - Left arrow turns BLUE and pulses
     *    - Capsule follows cursor vertically
     * 
     * 5. EXPAND BUTTON CLICKED:
     *    - Expands adjacent panel to full width
     *    - Other panel(s) collapse
     *    - Animation: slide out
     * 
     * 6. DOUBLE-CLICK DIVIDER:
     *    - Reset to equal widths (50/50 or 33/33/33)
     * ═══════════════════════════════════════════════════════════════════════ */
    
    return divider;
}

/* Create panel content with header */
static rx_view* create_panel_view(panel_id_t panel, float width_ratio) {
    rx_view* panel_container = vstack_new(0);
    if (!panel_container) return NULL;
    
    panel_container->box.background = NX_COLOR_BACKGROUND;
    /* Width is calculated based on ratio */
    /* In production: panel_container->box.flex_grow = width_ratio; */
    
    /* Panel header with title and controls */
    rx_view* header = hstack_new(12.0f);
    if (header) {
        header->box.background = (rx_color){ 35, 35, 37, 255 };
        header->box.padding = (rx_insets){ 12, 16, 12, 16 };
        
        /* Title */
        rx_text_view* title = text_view_new(panel_names[panel]);
        if (title) {
            text_view_set_font_size(title, 18.0f);
            title->font_weight = 600;
            title->color = NX_COLOR_TEXT;
            view_add_child(header, (rx_view*)title);
        }
        
        /* Spacer */
        rx_view* spacer = hstack_new(0);
        if (spacer) {
            spacer->box.flex_grow = 1.0f;
            view_add_child(header, spacer);
        }
        
        /* Width indicator */
        char width_str[16];
        snprintf(width_str, sizeof(width_str), "%.0f%%", width_ratio * 100);
        rx_text_view* width_label = text_view_new(width_str);
        if (width_label) {
            text_view_set_font_size(width_label, 11.0f);
            width_label->color = NX_COLOR_TEXT_DIM;
            view_add_child(header, (rx_view*)width_label);
        }
        
        view_add_child(panel_container, header);
    }
    
    /* Scrollable content area */
    rx_view* content = vstack_new(16.0f);
    if (content) {
        content->box.padding = insets_all(20.0f);
        
        /* Create actual panel content */
        rx_view* panel_content = NULL;
        switch (panel) {
            case PANEL_SYSTEM:     panel_content = system_panel_create(&g_settings); break;
            case PANEL_NETWORK:    panel_content = network_panel_create(&g_settings); break;
            case PANEL_BLUETOOTH:  panel_content = bluetooth_panel_create(&g_settings); break;
            case PANEL_DISPLAY:    panel_content = display_panel_create(&g_settings); break;
            case PANEL_SOUND:      panel_content = sound_panel_create(&g_settings); break;
            case PANEL_STORAGE:    panel_content = storage_panel_create(&g_settings); break;
            case PANEL_ACCOUNTS:   panel_content = accounts_panel_create(&g_settings); break;
            case PANEL_SECURITY:   panel_content = security_panel_create(&g_settings); break;
            case PANEL_PRIVACY:    panel_content = privacy_panel_create(&g_settings); break;
            case PANEL_APPEARANCE: panel_content = appearance_panel_create(&g_settings); break;
            case PANEL_POWER:      panel_content = power_panel_create(&g_settings); break;
            case PANEL_PROCESSES:  panel_content = processes_panel_create(&g_settings); break;
            case PANEL_STARTUP:    panel_content = startup_panel_create(&g_settings); break;
            case PANEL_SCHEDULER:  panel_content = scheduler_panel_create(&g_settings); break;
            case PANEL_EXTENSIONS: panel_content = extensions_panel_create(&g_settings); break;
            case PANEL_UPDATES:    panel_content = updates_panel_create(&g_settings); break;
            case PANEL_BOOTLOADER: panel_content = bootloader_panel_create(&g_settings); break;
            case PANEL_DEVELOPER:  panel_content = developer_panel_create(&g_settings); break;
            case PANEL_ABOUT:      panel_content = about_panel_create(&g_settings); break;
            case PANEL_KEYBOARD:   panel_content = keyboard_panel_create(&g_settings); break;
            case PANEL_TRACKPAD:   panel_content = trackpad_panel_create(&g_settings); break;
            case PANEL_COLOR:      panel_content = color_panel_create(&g_settings); break;
            case PANEL_APPS:       panel_content = apps_panel_create(&g_settings); break;
            case PANEL_DISPLAY_MANAGER: panel_content = display_manager_panel_create(&g_settings); break;
            case PANEL_DEVICE:     panel_content = device_panel_create(&g_settings); break;
            case PANEL_ICONS:      panel_content = icons_panel_create(&g_settings); break;
            case PANEL_WINDOWS:    panel_content = windows_panel_create(&g_settings); break;
            case PANEL_MOUSE:      panel_content = mouse_panel_create(&g_settings); break;
            case PANEL_DATE_TIME:  panel_content = date_time_panel_create(&g_settings); break;
            default:
                panel_content = (rx_view*)text_view_new("Select a panel");
                break;
        }
        
        if (panel_content) view_add_child(content, panel_content);
        view_add_child(panel_container, content);
    }
    
    return panel_container;
}

/* Create the split content area (1-3 panels side by side) */
static rx_view* create_split_content(void) {
    rx_view* split_area = hstack_new(0);
    if (!split_area) return NULL;
    
    split_area->box.background = NX_COLOR_BACKGROUND;
    split_area->box.flex_grow = 1.0f;
    
    for (int i = 0; i < g_tabs.split_count; i++) {
        int tab_idx = g_tabs.active_tabs[i];
        settings_tab_t* tab = &g_tabs.tabs[tab_idx];
        
        /* Panel view */
        rx_view* panel_view = create_panel_view(tab->panel, tab->width_ratio);
        if (panel_view) {
            view_add_child(split_area, panel_view);
        }
        
        /* Divider between panels (not after last) */
        if (i < g_tabs.split_count - 1) {
            rx_view* divider = create_divider(i);
            if (divider) {
                view_add_child(split_area, divider);
            }
        }
    }
    
    return split_area;
}

/* Create hover preview popup */
static rx_view* create_hover_preview(int tab_idx) {
    if (tab_idx < 0 || tab_idx >= g_tabs.tab_count) return NULL;
    
    rx_view* preview = vstack_new(8.0f);
    if (!preview) return NULL;
    
    preview->box.background = (rx_color){ 40, 40, 42, 240 };
    preview->box.padding = insets_all(12.0f);
    preview->box.corner_radius = corners_all(8.0f);
    preview->box.width = 250;
    
    /* Panel name */
    rx_text_view* name = text_view_new(panel_names[g_tabs.tabs[tab_idx].panel]);
    if (name) {
        text_view_set_font_size(name, 14.0f);
        name->font_weight = 600;
        name->color = NX_COLOR_TEXT;
        view_add_child(preview, (rx_view*)name);
    }
    
    /* Description */
    rx_text_view* desc = text_view_new(panel_descriptions[g_tabs.tabs[tab_idx].panel]);
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(preview, (rx_view*)desc);
    }
    
    /* Actions hint */
    rx_text_view* hint = text_view_new("Click to split • Right-click for options");
    if (hint) {
        text_view_set_font_size(hint, 10.0f);
        hint->color = (rx_color){ 120, 120, 122, 255 };
        view_add_child(preview, (rx_view*)hint);
    }
    
    return preview;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(void) {
    println("Starting NeolyxOS Settings...");
    
    /* Initialize context */
    memset(&g_settings, 0, sizeof(settings_ctx_t));
    g_settings.active_panel = PANEL_SYSTEM;
    settings_search_init(&g_settings);
    tabs_init();
    
    /* Create application */
    rx_app* app = app_new("NeolyxOS Settings");
    if (!app) {
        println("Failed to create application");
        return 1;
    }
    g_settings.app = app;
    
    /* Create main window */
    rx_window* window = app_create_window(app, "Settings", 1200, 750);
    if (!window) {
        println("Failed to create window");
        return 1;
    }
    g_settings.window = window;
    
    /* Create root layout */
    rx_view* root = hstack_new(0.0f);
    if (!root) {
        println("Failed to create root view");
        return 1;
    }
    root->box.background = NX_COLOR_BACKGROUND;
    
    /* Left sidebar - always visible */
    rx_view* sidebar = create_sidebar();
    if (sidebar) {
        g_settings.sidebar = sidebar;
        view_add_child(root, sidebar);
    }
    
    /* Main content area (tab bar + split panels) */
    rx_view* main_area = vstack_new(0);
    if (main_area) {
        main_area->box.flex_grow = 1.0f;
        
        /* Tab bar at top */
        rx_view* tab_bar = create_tab_bar();
        if (tab_bar) view_add_child(main_area, tab_bar);
        
        /* Split content area */
        rx_view* split_content = create_split_content();
        if (split_content) {
            g_settings.content_area = split_content;
            view_add_child(main_area, split_content);
        }
        
        view_add_child(root, main_area);
    }
    
    /* Set root view */
    window->root_view = root;
    
    println("Settings UI built with tabbed interface!");
    println("Features:");
    println("  - Multiple tabs (up to 8)");
    println("  - Split view (up to 3 panels)");
    println("  - Pop-out tabs to windows");
    println("  - Resizable dividers");
    println("  - Hover preview on tabs");
    println("  - Auto-split mode toggle");
    println("Launching app...");
    
    /* Run application event loop */
    app_run(app);
    
    println("Settings closed.");
    return 0;
}
