/*
 * NeolyxOS - System Settings: input & feedback panes
 *
 * Sound, Keyboard, Mouse & Trackpad, Notifications.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_internal.h"
#include "control_center.h"
#include "notification_center.h"

/*
 * A slider backed by an integer preference in [min, max]. The value is
 * cached for the session so dragging is smooth; it is saved (and
 * audited) once, when the drag ends.
 */
typedef struct { int loaded; float v; } pref_slider_t;

static int pref_slider(sui_t *ui, pref_slider_t *ps, const char *label, const char *sec, const char *key,
                       int min, int max, int def, const char *lo, const char *hi) {
    if (!ps || max <= min) return 0;
    if (!ps->loaded) {
        ps->v = (float)(sprefs_get_int(sec, key, def, min, max) - min) / (float)(max - min);
        ps->loaded = 1;
    }
    int r = sui_slider(ui, label, &ps->v, lo, hi);
    if (r == 2) {
        int iv = min + (int)(ps->v * (float)(max - min) + 0.5f);
        sprefs_set_int(sec, key, iv, min, max);
        ps->v = (float)(iv - min) / (float)(max - min);    /* snap to the stored step */
    }
    return r;
}

/* ============ Sound ============ */

static void build_sound(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "sound", "Sound", "Output volume and the sounds the system plays.");
    control_center_state_t *cc = control_center_get_state();

    sui_section(ui, "Output");
    sui_card_begin(ui);
    sui_status(ui, "Output device", "None detected", th->text_tertiary);
    if (cc) {
        float vol = (float)cc->volume / 100.0f;
        int r = sui_slider(ui, "Output volume", &vol, NULL, NULL);
        if (r) {
            int v = (int)(vol * 100.0f + 0.5f);
            control_center_set_volume(v);
            if (r == 2) sprefs_set_int("sound", "volume", v, 0, 100);
        }
    }
    sui_card_end(ui);
    sui_note(ui, "NeolyxOS does not expose an audio driver to the desktop yet. The volume level is kept by the shell, shared with Control Center, and used once audio output is available.");

    int startup = sprefs_get_bool("sound", "startup_sound", 1);
    int ui_fx = sprefs_get_bool("sound", "ui_effects", 1);
    sui_section(ui, "Sound Effects");
    sui_card_begin(ui);
    if (sui_toggle(ui, "Play sound on startup", NULL, &startup)) sprefs_set_bool("sound", "startup_sound", startup);
    if (sui_toggle(ui, "Play user interface sound effects", NULL, &ui_fx)) sprefs_set_bool("sound", "ui_effects", ui_fx);
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_sound, "sound", "Sound", build_sound);

/* ============ Keyboard ============ */

static pref_slider_t g_kb_rate, g_kb_delay;

static void build_keyboard(sui_t *ui) {
    sui_header(ui, "keyboard", "Keyboard", "Key repeat, function keys and the system keyboard shortcuts.");
    sui_card_begin(ui);
    pref_slider(ui, &g_kb_rate, "Key repeat rate", "keyboard", "repeat_rate", 1, 10, 6, "Slow", "Fast");
    pref_slider(ui, &g_kb_delay, "Delay until repeat", "keyboard", "repeat_delay", 1, 10, 5, "Long", "Short");
    int fkeys = sprefs_get_bool("keyboard", "standard_fn_keys", 0);
    if (sui_toggle(ui, "Use F1, F2, etc. keys as standard function keys", NULL, &fkeys))
        sprefs_set_bool("keyboard", "standard_fn_keys", fkeys);
    sui_card_end(ui);
    sui_note(ui, "Repeat settings are saved and applied when the keyboard driver accepts configuration.");

    sui_section(ui, "Keyboard Shortcuts");
    sui_card_begin(ui);
    sui_info(ui, "App Drawer", "NX");
    sui_info(ui, "Search", "NX + Space");
    sui_info(ui, "Tile window left / right", "NX + Left / Right");
    sui_info(ui, "Maximize / restore window", "NX + Up / Down");
    sui_info(ui, "Show keyboard help", "Hold NX");
    sui_card_end(ui);

    sui_section(ui, "Input");
    sui_card_begin(ui);
    sui_info(ui, "Layout", "U.S.");
    sui_info(ui, "Keyboard type", "PS/2 compatible");
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_keyboard, "keyboard", "Keyboard", build_keyboard);

/* ============ Mouse & Trackpad ============ */

static pref_slider_t g_mouse_speed, g_mouse_dbl;

static void build_mouse(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "mouse", "Mouse & Trackpad", "Pointer speed, scrolling direction and button behavior.");
    static const char *const buttons[] = { "left", "right" };
    static const char *const button_labels[] = { "Left", "Right" };
    sui_section(ui, "Mouse");
    sui_card_begin(ui);
    pref_slider(ui, &g_mouse_speed, "Tracking speed", "mouse", "tracking_speed", 1, 10, 5, "Slow", "Fast");
    pref_slider(ui, &g_mouse_dbl, "Double-click speed", "mouse", "double_click_speed", 1, 10, 5, "Slow", "Fast");
    int natural = sprefs_get_bool("mouse", "natural_scrolling", 0);
    if (sui_toggle(ui, "Natural scrolling", "Content follows the movement of your fingers", &natural))
        sprefs_set_bool("mouse", "natural_scrolling", natural);
    int primary = sprefs_get_choice("mouse", "primary_button", buttons, 2, 0);
    if (sui_segmented(ui, "Primary button", button_labels, 2, &primary))
        sprefs_set_choice("mouse", "primary_button", buttons, 2, primary);
    sui_card_end(ui);
    sui_note(ui, "Mouse preferences are saved and applied when the input driver accepts configuration; the pointer currently moves one-to-one with the device.");

    sui_section(ui, "Trackpad");
    sui_card_begin(ui);
    sui_status(ui, "Trackpad", "Not detected", th->text_tertiary);
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_mouse, "mouse", "Mouse & Trackpad", build_mouse);

/* ============ Notifications ============ */

static void build_notifications(sui_t *ui) {
    sui_header(ui, "notifications", "Notifications", "How apps and the system alert you, and when they stay quiet.");
    static const char *const previews[] = { "always", "unlocked", "never" };
    static const char *const preview_labels[] = { "Always", "Unlocked", "Never" };

    int dnd = sprefs_get_bool("notifications", "do_not_disturb", 0);
    int prev = sprefs_get_choice("notifications", "show_previews", previews, 3, 0);
    int locked = sprefs_get_bool("notifications", "allow_when_locked", 1);
    sui_card_begin(ui);
    if (sui_toggle(ui, "Do Not Disturb", "Silence banners until you turn it off", &dnd))
        sprefs_set_bool("notifications", "do_not_disturb", dnd);
    if (sui_segmented(ui, "Show previews", preview_labels, 3, &prev))
        sprefs_set_choice("notifications", "show_previews", previews, 3, prev);
    if (sui_toggle(ui, "Allow notifications on the lock screen", NULL, &locked))
        sprefs_set_bool("notifications", "allow_when_locked", locked);
    sui_card_end(ui);
    sui_note(ui, "These preferences are saved; Notification Center starts honoring them when it reads user preferences.");

    int count = notification_get_count();
    char n[16];
    sui_section(ui, "Notification Center");
    sui_card_begin(ui);
    sui_info(ui, "Notifications", sx_itoa(count < 0 ? 0 : count, n, (int)sizeof(n)));
    if (sui_button_row(ui, "Send a test notification", NULL, "Send Test", NXGLASS_BUTTON_DEFAULT, 1)) {
        uint32_t id = notification_push("System Settings", "Test notification",
                                        "Notifications are working.", 0xFF0A84FF);
        settings_log(id ? "notifications: test sent" : "notifications: test failed (center full?)");
    }
    if (sui_button_row(ui, "Remove all notifications", NULL, "Clear All", NXGLASS_BUTTON_DESTRUCTIVE, count > 0)) {
        notification_clear_all();
        settings_log("notifications: cleared all");
    }
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_notifications, "notifications", "Notifications", build_notifications);
