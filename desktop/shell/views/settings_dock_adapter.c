/*
 * NeolyxOS - System Settings: Dock adapter
 *
 * The only Settings file that talks to the dock. It uses what dock.h
 * exposes today: dock_get_state() (whose settings pointer is the shared
 * appearance_settings_t the dock reads every frame) and
 * dock_update_settings(). When the Dock area adds a dedicated settings
 * API, rewire the functions below; the Dock pane does not change.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_internal.h"
#include "dock.h"
#include "nxappearance.h"

#define DOCK_ICON_MIN 32
#define DOCK_ICON_MAX 80

static appearance_settings_t *dock_settings(void) {
    dock_state_t *d = dock_get_state();
    if (d && d->settings) return d->settings;
    return nxappearance_get();
}

int settings_dock_read(settings_dock_info_t *out) {
    if (!out) return -1;
    out->available = 0;
    out->icon_size = 48;
    out->magnification = 0;
    out->magnification_amount = 1.5f;
    out->autohide = 0;
    out->position = 0;
    out->position_supported = 0;
    out->app_count = 0;
    dock_state_t *d = dock_get_state();
    appearance_settings_t *a = dock_settings();
    if (!d || !a) return -1;
    out->available = 1;
    out->icon_size = a->dock_icon_size;
    if (out->icon_size < DOCK_ICON_MIN) out->icon_size = DOCK_ICON_MIN;
    if (out->icon_size > DOCK_ICON_MAX) out->icon_size = DOCK_ICON_MAX;
    out->magnification = a->dock_magnification ? 1 : 0;
    out->magnification_amount = a->dock_magnification_amount;
    if (out->magnification_amount < 1.0f) out->magnification_amount = 1.0f;
    if (out->magnification_amount > 2.0f) out->magnification_amount = 2.0f;
    out->autohide = a->dock_auto_hide ? 1 : 0;
    out->position = (int)d->position;
    if (out->position < 0 || out->position > 2) out->position = 0;
    out->position_supported = 0;   /* dock_render() lays out along the bottom only */
    out->app_count = d->icon_count;
    return 0;
}

int settings_dock_set_icon_size(int px) {
    appearance_settings_t *a = dock_settings();
    if (!a) return -1;
    if (px < DOCK_ICON_MIN || px > DOCK_ICON_MAX) return -1;
    a->dock_icon_size = px;
    dock_update_settings();
    return 0;
}

int settings_dock_set_magnification(int on, float amount) {
    appearance_settings_t *a = dock_settings();
    if (!a) return -1;
    if (amount < 1.0f || amount > 2.0f) return -1;
    a->dock_magnification = on ? true : false;
    a->dock_magnification_amount = amount;
    dock_update_settings();
    return 0;
}

int settings_dock_set_autohide(int on) {
    appearance_settings_t *a = dock_settings();
    if (!a) return -1;
    a->dock_auto_hide = on ? true : false;
    dock_update_settings();
    return 0;
}

int settings_dock_set_position(int position) {
    dock_state_t *d = dock_get_state();
    if (!d || position < 0 || position > 2) return -1;
    /* Stored on the dock state; dock_render() does not lay out vertically
     * yet, so the UI keeps this control disabled (position_supported). */
    d->position = (dock_position_t)position;
    dock_update_settings();
    return 0;
}
