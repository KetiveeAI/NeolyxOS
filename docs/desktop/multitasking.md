# NeolyxOS multitasking: windows, Overview, Stage, switcher

All of this runs in the userspace desktop shell (`desktop/`). It uses no kernel
calls beyond the existing framebuffer, input and time syscalls.

| Piece | Files |
|---|---|
| Window list, z-order, chrome, gestures, shortcuts | `desktop/shell/desktop_shell.c` (window sections) |
| Tiling geometry, spaces, snapshot cache, shell hooks | `desktop/shell/window_manager.c`, `desktop/include/window_manager.h` |
| Overview (Mission Control) | `desktop/shell/overview.c`, `desktop/include/overview.h` |
| Stage (lean Stage Manager) | `desktop/shell/stage.c`, `desktop/include/stage.h` |
| App switcher | `desktop/shell/switcher.c`, `desktop/include/switcher.h` |

## Keyboard shortcuts

NX is the system key (scancode 0x5B / 0x5C). Tapping NX alone still opens the
App Drawer, and holding it still shows the shortcut help. Any chord below
cancels both.

| Shortcut | Action |
|---|---|
| NX+Tab | App switcher. Keep NX held and press Tab to move right; Shift+Tab moves left. Release NX to activate. Esc cancels. |
| NX+Up | Overview (enter / leave) |
| NX+Shift+Up | Zoom the focused window (fill the work area / back) |
| NX+Left / NX+Right | Tile the focused window to the left / right half |
| NX+Down | Return a tiled or zoomed window to its own frame |
| NX+M | Minimize the focused window |
| NX+W | Close the focused window |
| NX+` | Next window of the same app (on this space) |
| NX+S | Stage on / off |
| NX+Space | Search (unchanged) |
| Esc / Return in Overview | Leave Overview (Return focuses the hovered window) |

NX+Up used to maximize. The brief assigns NX+Up to Overview, so maximize moved
to NX+Shift+Up. The green light and a title-bar double-click also zoom.

## Windows

- **Z-order** is an ordered list of ids. `z_order` mirrors each window's index
  in it, so values are unique (the old `z_order = window_count` collided).
- **Move** by the title bar. Movement is measured from where the press started,
  so it doesn't drift. The title bar stays below the menu bar, and at least
  80 px of the window stays on screen. Dragging a tiled or zoomed window away
  restores its own size under the pointer.
- **Snap**: push the pointer against the left or right edge for a half, near a
  corner for a quarter, or against the top for zoom. A glass preview springs
  out of the pointer, and releasing applies the tile with a spring.
- **Resize** from any edge or corner. The grab band runs from 6 px outside the
  frame to 3 px inside it, with 14 px corner zones. The minimum content size is
  `WM_MIN_W` x `WM_MIN_H` (320 x 180). Edge presses are handled before content
  routing, so app views can't swallow them.
- **Traffic lights** work on inactive windows too. Hovering the group shows all
  three colors with their glyphs, and a pressed light darkens.
- **Focus styling**: the focused window gets a stronger shadow, the active title
  bar and lit traffic lights. Inactive windows get a muted title bar and grey
  lights.
- **Animations** (all NXUI springs):
  - A new window fades in and rises 14 px.
  - Close and minimize draw a cached snapshot, scaling it down and fading it,
    or flying it to the app's dock icon. Restoring reverses the minimize.
  - Tile, zoom and Stage moves spring the live frame.
- **Close destroys the window** once its animation ends. The window is removed
  from the z-order, the window-manager state, the snapshot slot, Stage,
  Overview and the switcher, and the content mouse capture is cleared.
- **Cascade**: a new window opening exactly on top of another is offset.

## Overview

NX+Up or `overview_toggle()`. Every window on the current space springs into a
non-overlapping grid. The layout picks the column count that gives the largest
uniform scale (capped at 0.8). Rows follow the windows' vertical order and
columns their horizontal order. Each window gets its app icon and title in a
pill.

Along the top is a strip of spaces (up to `WM_MAX_SPACES` = 6):

- Click a space to switch to it.
- Click "+" to add a space.
- Hover a space to show its remove button. Removing a space moves its windows
  to the space before it.
- Drag a window onto a space, or onto "+", to move it there.

Click a window to focus it and leave. Click empty space, press Esc or press
NX+Up to leave.

Windows parked by Stage are included: they fly out of their thumbnail.

## Stage

NX+S or `stage_toggle()`. The focused window stays on stage, and every other
window on the space flies into a left strip. The strip groups windows by app,
with the most recent first, and shows up to `STAGE_MAX_GROUPS` (6) groups. Each
group is a fan of up to three thumbnails, tilted with a perspective slice and
badged with the app icon.

- **Click** a thumbnail to swap. The stage group flies into the strip and the
  chosen group flies onto the stage.
- **Drag** a thumbnail onto the stage to add its windows to the current group.
- **New windows** take the stage. Activating a parked window from the dock, the
  switcher or Overview brings its group forward.
- **Minimized windows** leave the strip. An empty stage pulls the most recent
  group forward.
- Turning Stage off returns every window to the frame it had before Stage.
- Switching space turns Stage off immediately, because Stage works on one space.

Stage is cheaper than redrawing every window each frame:

- **Thumbnails come from the snapshot cache.** Each window is captured into a
  slot of at most 480 x 340. A capture draws the window once into the
  backbuffer before the wallpaper overwrites it, then area-averages it down.
  Strip windows refresh at most every 1.5 s, at most one window per frame. A
  window marked changed (`wm_window_content_changed`) refreshes four times
  sooner.
- **The strip is two cached blits.** One is the glass panel from the NXUI panel
  cache. The other is a single layer surface holding every tilted thumbnail and
  icon. The layer is rebuilt only when its signature changes, which covers the
  groups, the snapshot generations, the appearance and the flights. Hover is a
  live ring and never rebuilds the layer.
- **With Stage off, the per-frame cost is zero.** `stage_tick`, `stage_render`
  and `stage_handle_mouse` return immediately. Snapshot use is a per-frame
  lease, so nothing is captured once the consumer stops asking.

## App switcher

NX+Tab opens a glass HUD with one icon per app, most recently used first
(z-order is the MRU order). It shows a dot per window: two dots mean several
windows. Minimized apps appear dimmed. Releasing NX activates the selection,
which also restores the window, switches its space or brings its Stage group
forward. Clicking an icon also activates it. Opening the switcher during
Overview leaves Overview first.

## Memory

The shell heap is a bump allocator with no frees, so each surface is allocated
on first use and then reused for good:

| Surface | Count | Size each | Total |
|---|---|---|---|
| Snapshot slot | 1 per concurrently tracked window (up to 16) | 480 x 340 x 4 = 653 KB | up to 10.4 MB |
| Stage strip layer | 1 | 196 x 692 x 4 = 542 KB | 542 KB |
| Overview mini wallpaper | 1 | 150 x 94 x 4 = 56 KB | 56 KB |

If an allocation fails, it is logged once and never retried. Windows then draw
as placeholder cards (chrome plus app icon), the strip draws live, and space
tiles draw plain.

## Hooks for other areas (`window_manager.h`)

- `wm_shell_window_order`, `wm_shell_get_frame`, `wm_shell_set_frame(…, animate)`,
  `wm_shell_raise`, `wm_shell_minimize`, `wm_shell_draw_window_image`
- `wm_snapshot_request / keep_fresh / get / generation`, `wm_window_content_changed`
- `wm_space_*`, `wm_window_space`, `wm_window_set_space`, `wm_window_hidden`

## Known limits

- The cursor has no shapes yet. Over a resize edge, a small double-arrow badge
  rides next to the arrow cursor. Real cursor shapes need an NXUI cursor API,
  which the Glass/NXUI owner would add.
- App views don't call `wm_window_content_changed` yet, so Stage thumbnails
  follow the 1.5 s refresh.
- PiP and sidebar modes (quick menu) are kept as they were. Resize is disabled
  for them.
