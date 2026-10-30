# Dock, taskbar and menu bar

Userspace shell components (`desktop/`). Nothing here runs in the kernel.

| File | Role |
|------|------|
| `desktop/include/dock.h` | Public API: lifecycle, input, pinned apps, settings API, shell hooks |
| `desktop/shell/dock.c` | Configuration and persistence, item model, dock layout and rendering, input, actions |
| `desktop/shell/dock_menu.c` | Right-click context menu (glass popover with an Options submenu) |
| `desktop/shell/taskbar_bar.c` | Taskbar layout (`DOCK_LAYOUT_TASKBAR`) |
| `desktop/shell/dock_internal.h` | Internal declarations shared by the three files above |
| `desktop/include/menubar.h`, `desktop/shell/menubar.c` | Menus, plus the full bar: focused app name and the status area |

## Layouts

**Dock** (`DOCK_LAYOUT_DOCK`, the default). A floating glass shelf (`NXGLASS_THIN`) at the
bottom, left or right edge. Left to right:

1. Pinned apps, in the user's order.
2. Running apps that are not pinned.
3. A separator.
4. Minimized windows (up to 8), each drawn as a small window card with its app icon.
5. The Trash.

**Taskbar** (`DOCK_LAYOUT_TASKBAR`). A full-width glass bar along the bottom, 48 px tall. It
shows the pinned app icons, a separator, then one pill per open window with its icon and
title. The focused window's pill has a highlighted fill and an accent underline. Pills for
minimized windows are dimmed. The taskbar is always at the bottom: the position setting
applies only to the dock layout.

## Behaviour

- **Running indicators** come from the shell's window table (`desktop_window_slot`). The dock
  hashes the id, flags and title of every slot each frame, which costs 16 slot reads. It
  rebuilds the item model only when that hash or the pinned list changes. A window belongs
  to an app when both have the same icon kind (`nxapp_icon_from_name`, plus local aliases
  such as "Task Manager"), or, for generic apps, the same name ignoring case.
  `dock_set_running` is kept so existing callers still build, but indicators no longer
  depend on it.
- **Magnification.** The pointer is mapped against the rest layout, never the magnified one,
  so the scale of each item depends only on the pointer and there is no feedback jitter.
  Each item scales by a smooth bump that reaches 2.6 icon slots either side of the pointer.
  The magnified row is anchored so the point under the pointer stays under it. Each item's
  displayed position and size follow their targets through critically damped `nxui_spring`s.
  An overall blend spring fades magnification in and out.
- **Tooltip.** A glass label (`NXGLASS_REGULAR`) above the hovered item, or beside it when the
  dock is on the left or right.
- **Click.** On a running app, this activates its frontmost window: the highest z-order among
  its visible windows, otherwise a minimized one (`desktop_activate_window`). On an app that
  is not running, it publishes `NX_EVENT_APP_LAUNCHED` and bounces the icon: two decaying
  hops over 0.9 s, skipped when `reduce_motion` is on. On a minimized window, it restores
  that window.
- **Drag to reorder.** Press on a pinned app and move the pointer more than 6 px. The other
  pinned apps slide to make room. Releasing commits the new order and saves it. Escape
  cancels the drag (needs hook 4 below).
- **Context menu** (right click). For an app: its windows, with the focused one checked, then
  Open, Show All Windows, Hide, Options > (Keep in Dock / Remove from Dock), and Quit. For a
  minimized window: Restore and Close. For the Trash: Open. Rows that do not apply are
  disabled. The menu stores its item's identity rather than its index, so if the window
  table changes while the menu is open, an action whose item no longer exists does nothing
  and is logged.
- **Autohide.** The dock or taskbar slides off its edge 0.45 s after the pointer leaves. It
  comes back when the pointer touches that edge. It stays out while the menu is open or a
  drag is in progress.
- **Fitting the screen.** If the row would not fit along the edge, icons are drawn smaller
  than the configured size.

## Settings API (for the Settings app)

```c
int dock_set_magnification(int on, float size);   /* on 0|1, size 1.0..2.0 (scale factor) */
int dock_set_icon_size(int px);                   /* 32..96 */
int dock_set_autohide(int on);                    /* 0|1 */
int dock_set_position(dock_position_t pos);       /* BOTTOM | LEFT | RIGHT */
int dock_set_layout(dock_layout_t layout);        /* DOCK | TASKBAR */
int dock_get_magnification(int *on, float *size);
int dock_get_icon_size(void);  int dock_get_autohide(void);
dock_position_t dock_get_position(void);  dock_layout_t dock_get_layout(void);
```

Every setter validates its arguments. An invalid value, including NaN, returns
`DOCK_ERR_INVALID` and changes nothing. A valid change:

1. takes effect on the next frame;
2. is copied into the dock fields of `nxappearance`;
3. is announced with `nxevent_publish_dock_resize`;
4. is logged as `[DOCK] ...`;
5. is saved through nxconfig.

`DOCK_ERR_PERSIST` means the change took effect but could not be saved.

Values are stored in the user config `NXC_USER_DEFAULTS`:

| Section | Key | Values |
|---------|-----|--------|
| `[dock]` | `icon_size` | 32..96 |
| `[dock]` | `magnification` | `true` / `false` |
| `[dock]` | `magnification_size` | 100..200 (percent) |
| `[dock]` | `autohide` | `true` / `false` |
| `[dock]` | `layout` | `dock` / `taskbar` |
| `[dock]` | `pinned` | comma-separated app names |
| `[desktop]` | `dock_position` | `bottom` / `left` / `right` |

Saved values are validated when they are loaded. A value that is out of range is ignored
with a warning, and the default is used instead.

## Menu bar

`menubar_render_bar(c, w, h, clock_text)` draws the whole bar:

- **Left:** the focused window's title in bold (`desktop_focused_window_id`; a minimized
  window counts as no focus, which shows "NeolyxOS"), followed by the menus.
- **Right:** `[battery] [volume] [Wi-Fi or Ethernet]  clock  [bell] [Control Center]`.

The Control Center item is centered at `w - 35` and the bell at `w - 67`
(`MENUBAR_CC_CENTER`, `MENUBAR_NC_CENTER`). These positions match the shell's click zones
(`w-50..w-20` and `w-80..w-55`), which are unchanged.

Where each status item gets its state:

- **Volume and Wi-Fi:** the Control Center state (`control_center_get_state`). If Wi-Fi is off
  but Ethernet is up, the Ethernet glyph is shown.
- **Battery:** shown only after `menubar_set_power_status(percent, charging)` reports a
  battery. No battery syscall exists yet. The host preview uses a demo value of 82 %, set
  under `#ifdef NX_HOST_PREVIEW`.

## Shell hooks (lead to apply in `desktop_shell.c`)

The dock and menu bar compile and run without these hooks. Without them:

- the shell keeps its own fixed status bar;
- Hide is shown disabled;
- minimize animations aim at the screen center;
- Escape does not close the dock menu.

The ready-to-apply diff is `neolyx-desktop/dock/desktop_shell_dock_hooks.patch`. It contains
four hooks:

1. **`render_navigation_bar`:** keep the clock formatting, then call
   `menubar_render_bar(c, (float)g_desktop.width, th->menubar_height, line)` in place of the
   hand-drawn bar.
2. **Hide handler:** add a `static int desktop_dock_hide_window(uint32_t id)` that minimizes
   one window. It uses `window_by_id`, `dock_minimize_target`, `wm_minimize_window`,
   `wm_add_minimized` and sets `minimized = 1`. Register it right after `dock_init` with
   `dock_set_hide_handler(desktop_dock_hide_window)`.
3. **Minimize targets:** in the title-bar minimize button and in menu action 17, call
   `dock_minimize_target(&x, &y)` after the default center so windows fly to the dock's
   minimized section, or to the taskbar.
4. **Escape:** in `desktop_handle_key`, after `menubar_handle_key`, add
   `if (dock_handle_key(scancode, pressed)) return;`.

## Known limitations

- `nxc_config_load` / `nxc_config_save` do not touch storage yet (VFS TODO in
  `lib/nxconfig.c`), so settings last only until reboot. `nxc_config_load` also takes one
  of only 4 static slots on every call, so the dock loads the config once and keeps the
  handle.
- Closing a window (close button, `desktop_close_window`) plays the fade animation, but the
  window slot is never destroyed. The dock therefore keeps showing the app as running.
  This belongs to the window code in `desktop_shell.c` / `window_manager.c`.
- The taskbar has no drag-to-reorder and no launch bounce.
- There is no battery-status syscall. `menubar_set_power_status` is the entry point for a
  future power service.
