# Compositor efficiency, part 1 (R4)

The desktop now draws only what changed and only what can be seen. No window protocol, kernel ABI or public API change. Every pixel that ends up on screen is the same as before.

## What was slow

QEMU, 2 GiB, one CPU, software emulation (TCG). Five windows open: System Settings, Shell, two nxwtest windows and nxuitest.

| | Before | After |
|---|---|---|
| Idle, no input: draw time per drawn frame | 148–242 ms | 4–5 ms |
| Idle: frames drawn in 5 s | 21–34, every tick, the CPU never rests | 270–278 small partial frames for the animating nxwtest window |
| Dragging a window (full frames): draw time per frame | 179–204 ms | 102–104 ms |

Absolute times are high because QEMU emulates the CPU here. The user's machine runs QEMU with KVM, which is much faster, but the ratios hold. Three things cost the most.

1. **The shell's own app windows were redrawn every tick.** Settings, Shell, Notes, Task Manager, Path and Fonts are drawn by the shell, not by a client. The frame loop damaged their whole frame every 16 ms, because their content could animate. With Settings open, the desktop redrew about 500,000 pixels per tick and never went idle.
2. **Rounded clips were evaluated per pixel across whole rows.** Every window body is drawn inside `nxc_clip_rrect`. A span that touched the window's left or right edge computed the rounded clip's coverage (a signed distance, with a square root in the corners) for every pixel of the span, not only near the edge. The Settings sidebar background alone cost 37–66 million cycles per draw.
3. **Every pass drew everything.** A partial frame draws one pass per damaged rectangle. Each pass ran every component's drawing code, clipped. A 200×120 damage inside one client window still laid out and drew every window below it, and the drop shadow under each window was blended over the whole window area before the body covered it.

## What changed

### App windows redraw on demand (`desktop/shell/desktop_shell.c`)

A shell-drawn window is redrawn when:
- the shell sees activity (input, a running nxui animation, a window change), as before;
- a frame redraws the part of the screen it covers;
- it asks, with `desktop_window_redraw_in(window_id, delay_ms)` (`desktop_shell.h`), from its render callback;
- at least every `DESKTOP_VIEW_REFRESH_MS` (1 s), as a safety net for content that changes without asking.

The earliest request wins. A request is used up by the redraw it causes, so a view that keeps changing asks again each time it draws. `nxui_ms_to_next_step(rate)` (`nxui.h`) gives the time to the next step of a blink.

| View | Asks for a redraw at |
|---|---|
| Shell | the next caret blink, while focused |
| Notes | the next caret blink while focused; the autosave time; when a notice leaves the status bar |
| Font Manager | the next caret blink in the sample field |
| Settings | when the overlay scrollbar starts to fade, then every tick until it is gone |
| Task Manager | every tick on Processes and Performance (their graphs scroll smoothly between samples); otherwise the next sample, a status message's expiry, or the end of an app's grace period |

Uptime and the network list in Settings refresh through the 1 s safety net. That matches how often they change.

### Rounded clip: per-pixel only near the edge (`desktop/lib/nxcanvas/nxc_core.c`)

`rclip_interior_x` gives the run of each row whose pixel centers are at least a pixel inside the rounded clip, where its coverage is exactly 255. Only the pixels outside that run are evaluated one by one. For solid fills the run takes the plain fill path. The margin is the same one-pixel margin the old row test used. So the output is bit for bit the same:
- the new `test_rclip` stress image has the same hash on the old and the new engine;
- all 42 images of the existing NXCanvas suites are identical.

### Pass culling (`desktop/shell/desktop_shell.c`)

Before each component is drawn, the pass checks whether any of it can show. It skips the component when its bounds miss the pass rectangle, or when the visible part lies inside the opaque interior of a window drawn above it.

- **What counts as opaque.** A window is opaque when it is drawn normally: no open, close or minimize animation, full opacity, and not PiP or a sidebar tab. The theme's window background must also be opaque. `draw_window_body` fills the rounded frame with that background before anything else.
- **The interior.** The frame inset past the corner curves, plus one pixel.
- **What is culled.**
  - Windows, by bounds (frame plus deepest shadow) and by occlusion.
  - The menu bar and clock widget, by bounds.
  - The search field and desktop icons, when an opaque window covers the whole pass.
- **What always draws.**
  - The tile preview and the Stage strip. They step their own animations and keep snapshots fresh.
  - Overview.
  - Anything drawn above the windows.

### Shadows only around the window (`window_shadow`)

When the window body will be opaque, the drop shadow is drawn through four clip bands around the window's interior instead of under the whole window. It is the same cached mask, so every visible pixel gets the same value.

### Frame pacing

It needed no change. FRAME_DONE already goes out right after the tick that presented a commit, ticks stay on a steady 16 ms grid, and a client message wakes the loop at once. With the work above, the desktop now finishes its ticks well inside 16 ms when idle, so clients get FRAME_DONE on time.

## Checking it

- **Debug self-check.** `make EXTRA_CFLAGS=-DDESKTOP_DAMAGE_VERIFY` redraws every frame a second time with culling and shadow bands off, then compares. Over the benchmark (open 5 windows, idle, drag) it found 0 wrong pixels outside client windows.
  - Its view-window report now names the topmost window under each differing pixel. A client drawing ahead of its commit is no longer counted against the shell window under it.
  - Differences inside client windows are the known case of a client drawing its next frame before it commits.
- **Screenshot comparison.** The same scene (Settings and Shell open) on main and on this change differs only in the clock and the Shell caret's blink phase.
- **Regressions.** fbtest, wintest (39 PASS), nxwtest and nxuitest give the same results as on main.
- **Frame log.** The `[DESKTOP] Frames` line now comes at least every 5 s, so slow frames still report.

## Not in this part

- **Opaque-region hints from clients.** These need a new window protocol request, which needs owner sign-off. Client buffers are opaque XRGB today, so the window body already makes them opaque.
- **Caching window content between frames.** A window that moves still redraws its content. The shell-drawn views move out of process in the 1.0 plan, and client windows already composite from their buffers.
