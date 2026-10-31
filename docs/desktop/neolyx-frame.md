# Neolyx Frame: the NeolyxOS app layout

Status: proposal (prototype in Settings, Path and Task Manager). Shared code: `desktop/include/nxframe.h`, `desktop/lib/nxframe.c`.

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
