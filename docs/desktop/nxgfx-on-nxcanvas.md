# NXGFX on NXCanvas (R1)

NxUI apps draw through `sdk/nxui` → `gui/nxrender_c` (reox_ffi, widgets) → NXGFX. Before R1, NXGFX drew text with a built-in 8×16 bitmap font scaled by whole steps and drew shapes with integer rasterizers, so app windows looked blocky next to the desktop shell. R1 routes NXGFX through NXCanvas (`desktop/lib/nxcanvas`), the engine the shell already uses.

No kernel ABI, window protocol, or existing function signature changes.

## What changed

| NXGFX call | Before | Now |
|---|---|---|
| `nxgfx_draw_text`, `nxgfx_measure_text` | 8×16 bitmap, ASCII only, fixed 8 px advance × integer scale | NX Sans Text outlines through NXCanvas: anti-aliased, kerned, UTF-8, subpixel x positioning, vertical hinting; width is the true advance, height is ascent + descent |
| `nxgfx_fill_rounded_rect` | 3 rects + 4 approximate circles (overlapping seams) | one rounded rect with exact edge coverage |
| `nxgfx_fill_circle` | 2 px distance band | exact area coverage |
| `nxgfx_draw_line` | Wu line, width ignored | axis-aligned: exact pixel rows/columns, `w` thick; diagonal: anti-aliased, `w` wide, round caps |
| `nxgfx_draw_arc`, `nxgfx_draw_thick_arc` | chained 1 px segments | one stroked arc, `thickness` wide, round caps |
| `nxgfx_fill_rect` with alpha < 255 | overwrote pixels | blends (alpha 0 draws nothing); opaque fills keep the exact integer path |

Text position semantics are unchanged: `pos` is the top-left of the line box. The baseline is `round(ascent)` below it.

Everything else in NXGFX (gradients, images, paths, effects) is untouched.

## New functions (`include/nxgfx/nxgfx.h`)

- `int nxgfx_font_register(const uint8_t* data, uint32_t size)`: hand NXGFX the TrueType data for its UI face. The data must stay valid for the life of the process. Returns 0, or -1 with a log line when rejected (NULL, outside 1 KiB–8 MiB, or not a usable TrueType font).
- `bool nxgfx_engine_ready(void)`: true once the NXCanvas engine runs.
- `void nxgfx_set_log_sink(void (*)(const char*))`: diagnostics sink. `reox_ffi_set_log_sink` forwards to it.

## Font loading (owner decision: load from disk)

`rebuild_image.sh` installs `resources/fonts/NXSans/NXSans-Text.ttf` as `/System/Fonts/NXSans-Text.ttf` on the boot image. When an NxUI app starts its window (`nxui_run`), `sdk/nxui/nxui.c` reads that file once (≤ 4 MiB), registers it, and logs `[NXUI] UI font: NX Sans Text (outline)`.

If the file is missing, unreadable, or rejected, the app still runs with the bitmap font and logs why (`[NXUI] nxui_run: cannot open /System/Fonts/NXSans-Text.ttf; using the bitmap font (<error code>)`).

## Fallbacks

The pre-R1 code paths remain and are used, with one log line each, when:

- the NXCanvas engine cannot start (allocation failure): legacy rasterizers and bitmap font;
- a context is larger than 4096×4096 or its pitch is not a whole number of pixels: legacy rasterizers for that context;
- no font is registered: bitmap font.

## Memory per app

Allocated once, at the first `nxgfx_init`: NXCanvas scratch rows for 4096-pixel-wide surfaces (about 0.7 MiB), a 1 MiB glyph cache and a 256 KiB shadow cache, plus the 372 KiB font file. That's about 2.3 MiB of the 64 MiB heap reserve. Drawing does not allocate.

## Tests

- Host: `tests/host/nxgfx_canvas_test.c` (31 checks: bitmap fallback, bad-font rejection, proportional outline text, line-box height, anti-aliased edges, exact opaque fills, blending, hairlines, clipping, UTF-8). Build and run instructions are in the file header. Built with `-DNXGFX_LEGACY` against a pre-R1 library, it renders the same scene the old way for comparison.
- QEMU: boot, open `/apps/nxuitest` from the app drawer, click a button and type. Serial shows the engine and font lines above and `[NXUITEST] action 3`, `key 104`, `key 105`.
