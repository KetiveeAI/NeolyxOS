/*
 * NeolyxOS - System Settings: Dock adapter
 *
 * The only Settings file that talks to the dock. It goes through the
 * dock's settings API (dock.h), which validates, applies, persists
 * ([dock] section) and logs every change.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_internal.h"
#include "dock.h"

int settings_dock_read(settings_dock_info_t *out) {
    if (!out) return -1;
    dock_state_t *d = dock_get_state();
    out->available = d != 0;
    out->icon_size = dock_get_icon_size();
    if (dock_get_magnification(&out->magnification, &out->magnification_amount) != DOCK_OK) {
        out->magnification = 0;
        out->magnification_amount = 1.5f;
    }
    out->autohide = dock_get_autohide();
    out->position = (int)dock_get_position();
    if (out->position < 0 || out->position > 2) out->position = 0;
    out->position_supported = 1;
    out->layout = (int)dock_get_layout();
    out->app_count = d ? d->icon_count : 0;
    return d ? 0 : -1;
}

static int dock_rc(int rc) { return (rc == DOCK_OK || rc == DOCK_ERR_PERSIST) ? 0 : -1; }

int settings_dock_set_icon_size(int px) { return dock_rc(dock_set_icon_size(px)); }
int settings_dock_set_magnification(int on, float amount) { return dock_rc(dock_set_magnification(on ? 1 : 0, amount)); }
int settings_dock_set_autohide(int on) { return dock_rc(dock_set_autohide(on ? 1 : 0)); }

int settings_dock_set_position(int position) {
    if (position < 0 || position > 2) return -1;
    return dock_rc(dock_set_position((dock_position_t)position));
}

int settings_dock_set_layout(int layout) {
    if (layout < 0 || layout > 1) return -1;
    return dock_rc(dock_set_layout((dock_layout_t)layout));
}
