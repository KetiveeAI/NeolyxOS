# Neolyx Frame: the NeolyxOS app layout

Status: adopted 2026-10-31. Used by the window chrome, Settings (all panes via sui), Network, Path, Shell, Font Manager, Task Manager and Setup. Shared code: `desktop/include/nxframe.h`, `desktop/lib/nxframe.c`.

NeolyxOS apps should be recognisable as NeolyxOS at a glance, not read as a copy of another desktop. The Neolyx Frame is the layout every app window is built from.

## The six parts

| Part | What it looks like | Replaces |
|------|--------------------|----------|
| Crest | 2px saffron line along the top edge of the focused window only | nothing (focus was only a shadow change) |
| Title | App glyph + **App** / Place, anchored left. Window controls stay on the right. | centered window title |
| Rail | Flush navigation column in a flat tone, UPPERCASE group labels, monochrome glyphs; the selected item gets a saffron bar and a soft saffron wash | sidebar of coloured icon tiles with a solid accent pill |
| Page title | Large title with a short saffron tick under it, subtitle below | coloured icon tile beside the title |
| Ledger | Sections are an UPPERCASE label with a rule; rows sit on the page as label / value columns, values left-aligned | floating rounded cards with right-aligned grey values |
| Strip | Status strip across the bottom of the app: context on the left, actions on the right | per-app ad hoc footers |

## Rules

- Window controls stay on the right of the title bar (owner decision, 2026-10-31).
- Apps report where the user is with `desktop_set_window_place(win_id, "Network")`; the title bar shows "App / Place".
- Selection anywhere (rail, lists, tables) uses the same mark: saffron bar + saffron wash, text keeps its normal colour.
- Desktop/GUI code stays in userspace. Nothing here touches the kernel.

## Kit reference (`nxframe.h`)

| Function | Use |
|----------|-----|
| `nxframe_crest`, `nxframe_title` | window manager only |
| `nxframe_rail`, `nxframe_rail_group`, `nxframe_rail_item` | app navigation column (`NXFRAME_SYM_SLOT` when the app draws its own glyph) |
| `nxframe_page_title`, `nxframe_section`, `nxframe_ledger_row`, `nxframe_rule` | page content |
| `nxframe_tabs` | app-level tabs (hit-test with `nxglass_segment_at`); keep `nxglass_segmented` for form choices such as DHCP / Manual |
| `nxframe_panel` | flat surface inside a window (replaces `nxglass_card` in apps; glass stays for menus, Control Center and overlays) |
| `nxframe_selection`, `nxframe_row_wash` | selected and hovered rows |
| `nxframe_caps` | uppercase captions and column headers |
| `nxframe_strip` | bottom status strip |
