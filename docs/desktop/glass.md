# NXGlass: design tokens and usage

NXGlass is the shared glass layer for the NeolyxOS desktop. Every shell
surface and shell-hosted app view draws its panels and controls through it,
so one change to the glass look applies everywhere.

- API: `desktop/include/nxglass.h`
- Implementation: `desktop/lib/nxglass.c`
- Built on NXUI (`desktop/include/nxui.h`) for the theme, text, motion and
  the panel cache, and on NXCanvas for drawing.
- Runs in userspace only. It never touches the kernel or the framebuffer
  directly; it draws into `nxui_canvas()`.

## 1. How a glass surface is drawn

Every glass panel is built from the same layers, drawn in this order:

1. **Shadow**: `nxui_elevation(level)`. Elevation 0 is no shadow, 1 is a control, 2 is a popover or panel, 3 is an inactive window and 4 is an active window, sheet or dialog.
2. **Backdrop**: the pre-blurred wallpaper (sigma 26) seen through the
   panel. It is saturated (about 1.5 to 1.85x), then tinted, then given
   a light grain so gradients do not band.
3. **Sheen**: a soft diagonal wash of white from the top-left corner. Its
   reach is capped at about 360 px, so large panels are not washed out.
4. **Inner glow**: three 1 px hairlines stepping inward and fading out.
5. **Rim light**: a 1 px ring whose gradient is brightest at the top-left.
   It fades to a faint rim at the bottom-right. In light appearance the
   bottom of the rim turns slightly dark.
6. **Edge**: a 1 px dark hairline just outside the panel. It keeps the
   glass separate from busy or bright backdrops.

Static panels (`live = 0`) go through the NXUI panel cache with layers 1
to 6 baked in, so they cost one blit per frame.

## 2. Glass levels

| Level | Use | Dark tint (alpha, saturation) | Light tint (alpha, saturation) |
|---|---|---|---|
| `NXGLASS_ULTRATHIN` | HUDs over content (volume, appearance, keyboard) | navy 84, 1.80 | white 150, 1.85 |
| `NXGLASS_THIN` | menus, popovers, dock, tooltips | navy 120, 1.70 | white 174, 1.80 |
| `NXGLASS_REGULAR` | panels, Control Center, Notification Center, popovers with arrows | navy 160, 1.60 | white 194, 1.75 |
| `NXGLASS_THICK` | window sidebars, sheets, dialogs, setup cards | navy 202, 1.45 | white 214, 1.55 |
| `NXGLASS_CHROME` | menu bar, title bars | deep navy 150, 1.50 | white 186, 1.60 |

The rim, sheen and glow get weaker as the glass gets thicker. Chrome has
no inner glow. The exact values are in `g_spec_dark` and `g_spec_light`
in `nxglass.c`.

**Accent-tinted glass**: `nxglass_panel_tinted(..., tint)` blends a
color into the level's tint: 30% in dark appearance, 22% in light. Use it
for selected or branded surfaces, such as a focused setup card.

## 3. Tokens: `nxglass_tokens()`

Tokens are rebuilt on their own when the appearance or the accent
changes. Use these instead of raw colors.

| Token | Use |
|---|---|
| `label`, `label_secondary`, `label_tertiary`, `label_quaternary` | Text on glass. These colors are vibrancy-friendly: the dark-appearance labels are translucent white, so they take on the hue of the backdrop. |
| `fill`, `fill_hover`, `fill_pressed` | Translucent fills for controls at rest, on hover and when pressed. |
| `fill_inset` | Text fields, checkboxes and other wells. |
| `raised` | The selected segment, white keys in light appearance. |
| `rim_top`, `rim_bottom`, `edge`, `separator` | Light and lines. |
| `accent`, `accent_hover`, `accent_pressed`, `accent_soft`, `focus_ring`, `on_accent` | The accent (Vajra saffron) and its states. |
| `success`, `warning`, `destructive`, `info` | Status colors. `warning` is brighter in dark appearance so it stays readable. |
| `radius_small` (6), `radius_control` (8), `radius_card` (12), `radius_panel` (14), `radius_sheet` (18) | Corner radii. |
| `spacing` (8) | The base grid unit. |

## 4. Surfaces

| Function | Notes |
|---|---|
| `nxglass_panel(c, r, radius, level, elevation, live)` | The standard glass. Use `live = 0` unless the panel's rect changes every frame. |
| `nxglass_panel_tinted(...)` | Accent-tinted glass. |
| `nxglass_rim(c, r, radius, level)` | The light treatment alone, for surfaces you fill yourself. |
| `nxglass_scrim(c, r, amount)` | A dimming layer behind modal content. Animate `amount` from 0 to 1. |
| `nxglass_popover(c, r, arrow, arrow_pos, live)` | REGULAR glass with an arrow on one edge. The arrow is filled with the panel's own color. |
| `nxglass_sheet(c, r, live)` | THICK glass with square top corners, attached under a title bar. |
| `nxglass_dialog(...)`, `nxglass_dialog_rect(...)`, `nxglass_dialog_button_at(...)` | An alert with an icon, a title, wrapped message text and up to 3 buttons laid out right to left. |
| `nxglass_tooltip(c, ax, ay, text)` | THIN glass below the anchor point, kept on screen. |
| `nxglass_card(c, r, radius)` | A grouped card inside a panel or window. |
| `nxglass_sidebar(c, r)` | The left column of a window. |
| `nxglass_separator(c, x, y, w)` | A horizontal hairline. |
| `nxglass_titlebar(c, r, radius, title, focused, hovered)`, `nxglass_titlebar_button_at(...)` | Glass window chrome. The traffic lights sit at x 14, 30 and 46, which match the shell's hit boxes. |
| `nxglass_hud(c, r, sym, sym_value, title, subtitle, level, sym_color, opacity)` | The OSD card used for volume, appearance and lock keys. |

## 5. Controls

All controls are immediate-mode. You pass state flags and keep your own
model. The flags are `NXGLASS_HOVER`, `NXGLASS_PRESSED`, `NXGLASS_FOCUSED`, `NXGLASS_DISABLED` and `NXGLASS_SELECTED`. Every control has a matching hit helper.

| Control | Hit helper |
|---|---|
| `nxglass_button` (default, primary, destructive, plain) | `nxglass_hit(r, x, y)` |
| `nxglass_toggle(c, x, y, on_t, state)`: `on_t` animates the knob (ease in-out). Pressing stretches the knob. | `nxglass_toggle_rect(x, y)` |
| `nxglass_checkbox`, `nxglass_radio`: `checked_t` animates; returns the hit rect, label included | the `*hit` out-parameter |
| `nxglass_slider(c, r, value, state)` | `nxglass_slider_value_at(r, x)` |
| `nxglass_slider_large(c, r, value, sym, sym_value, state)`: a Control Center module slider | your own mapping: fill = h + (w - h) * value |
| `nxglass_segmented(c, r, labels, n, selected, hovered)` | `nxglass_segment_at(...)` |
| `nxglass_stepper(c, x, y, hovered, pressed, minus_state, plus_state)` | `nxglass_stepper_part_at(...)` |
| `nxglass_row(c, r, icon, title, detail, state)`: a list or sidebar row | `nxglass_hit` |
| `nxglass_tile(c, r, sym, title, subtitle, on_color, chevron, state)`: a Control Center tile; `SELECTED` means on | `nxglass_hit` |
| `nxglass_field(c, r, text, placeholder, search, state)`: has a caret blink while focused | `nxglass_hit` |
| `nxglass_progress(c, r, value)`: a value below 0 makes it indeterminate | n/a |
| `nxglass_badge(c, x, y, label, color)` | n/a |
| `nxglass_icon_button(c, r, sym, state)`: round, with an NXSYM symbol | `nxglass_hit` |
| `nxglass_scrollbar(c, track, content, view, offset, state)`: an overlay scrollbar that is thin when idle | `nxglass_scrollbar_thumb(...)`, `nxglass_scrollbar_offset_at(...)` |
| `nxglass_section_header(c, x, y, w, title)`: small-caps text above a group | n/a |

Standard sizes:

- Button height: 28
- Row height: 36
- Field height: 30
- Toggle: 38 x 22
- Checkbox: 16
- Stepper: 64 x 24
- Icon button: 28
- Scrollbar width: 10

## 6. Performance rules

- **Keep static glass static.** Draw a panel at its resting rect with
  `live = 0`. To slide it in, use `nxc_translate`; don't move the rect
  itself. A fade with `nxc_set_alpha` reuses the cached panel too. Control
  Center and Notification Center work this way.
- **The panel cache** (`nxui_panel_ex`, 16 slots) keys on the integer
  rect, radius, material, elevation, appearance and the decoration. When a
  slot is rendered, two things are measured once:
  - the bounds of its visible pixels, so blits skip the empty shadow tail;
  - its opaque interior, which is copied row by row instead of blended.
    This fast path applies when the canvas alpha is 255, the translation is
    a whole number of pixels and there is no rounded clip.
- **Use `live = 1` only** when the rect really changes every frame, such
  as a window being resized.
- **Avoid live blur.** All materials sample the pre-blurred wallpaper.
  NXGlass never blurs per frame.
- **Mind the slot count.** Avoid more than about 12 distinct static
  panels on screen at once. The shell itself uses about 5 (menu bar, dock,
  clock widget, search and one popover).
- **Watch for thrashing.** `nxui_panel_stats(&hits, &misses)` reports
  cache behavior. The gallery prints these counts. If misses keep
  climbing while nothing moves, the cache is thrashing.

Measured with the host preview at 1280x800, as callgrind instructions per
frame, against the shared-seams base commit:

| Scene | Before | After |
|---|---|---|
| Idle desktop | 21.8M | 19.8M |
| Control Center + Notification Center open | 56.3M | 53.5M |
| Keyboard shortcut sheet | 120.5M | 63.9M |

## 7. Gallery (design review)

`nxglass_gallery_toggle()` shows a full-screen gallery of every level,
control and chrome element in the current appearance. The gallery is
drawn by `nxglass_gallery_render()`, which `keyboard_osd_help_render()`
calls as the last overlay of each frame, so `desktop_shell.c` needed no
new hook. `nxglass_gallery_light()` and `nxglass_gallery_dark()` switch
the appearance for screenshots.

To capture the gallery with the host preview:

```
make -C desktop/tools/preview OBJ=out/glass
cat > /tmp/gal.txt <<'EOF'
3 call nxglass_gallery_toggle
30 shot gallery_dark.png
31 call nxglass_gallery_light
50 shot gallery_light.png
51 end
EOF
desktop/tools/preview/out/glass/preview -n 60 -s /tmp/gal.txt
```

## 8. Rules for callers

- Draw surfaces and controls through NXGlass. Don't hand-roll fills. If
  you need a control that NXGlass lacks, draw it locally and tell the Glass
  area, which will add it here.
- Use the tokens instead of raw colors. Check your view in both
  appearances.
- Balance every `nxc_save` with an `nxc_restore`. NXGlass balances its own
  saves and restores.
- Existing signatures in `nxglass.h` never change. Additions only.
