# NeolyxOS fonts

How the desktop renders text, which faces ship with the system, how user
fonts are installed and how the Font Manager uses all of it.

All of this runs in userspace (`desktop/`). The kernel only provides the
framebuffer and file syscalls.

## Components

| Component | File | Role |
|-----------|------|------|
| TrueType engine | `desktop/lib/nxcanvas/nxc_font.c` | Parses and validates fonts: table directory, `head`, `maxp`, `hhea`/`hmtx`, `loca`/`glyf`, `cmap` 4/12, OS/2, `kern`, GPOS kerning, `name`, `post` |
| Text renderer | `desktop/lib/nxcanvas/nxc_text.c` | Layout, kerning, fallback, glyph cache, hinting, coverage correction |
| Public API | `desktop/include/nxcanvas.h` | `nxc_font_*`, `nxc_draw_text`, `nxc_text_*` |
| System faces and type scale | `desktop/shell/nxui.c` (Typography section), `desktop/shell/nxui_assets.S` | Embeds and loads NX Sans, sets fallback chains, registers the faces |
| Font registry | `desktop/lib/nxfont_registry.c`, `desktop/include/nxfont_registry.h` | Installed system and user fonts: list, look up, install, enable or disable |
| Font Manager | `desktop/shell/views/fonts_view.c` | The app (`launch Font_Manager` in the preview) |

## System family: NX Sans

Built into the desktop image by `nxui_assets.S` from `resources/fonts/`
(see `nxsans-manifest.json`). NX Sans is based on Source Sans 3; NX Sans Mono
is based on Cascadia Mono. Both use the SIL Open Font License 1.1.

| `nxui_font_id_t` | File | Weight | Registry style |
|------------------|------|--------|----------------|
| `NXUI_FONT_REGULAR` (0) | NXSans-Text.ttf | 400 | NX Sans Regular |
| `NXUI_FONT_MEDIUM` (1) | NXSans-UI.ttf | 500 | NX Sans Medium |
| `NXUI_FONT_BOLD` (2) | NXSans-Bold.ttf | 700 | NX Sans Bold |
| `NXUI_FONT_DISPLAY` (3) | NXSans-Display.ttf | 600 | NX Sans Semibold |
| `NXUI_FONT_MONO` (4) | NXSans-Mono.ttf | 400 | NX Sans Mono Regular |
| `NXUI_FONT_SEMIBOLD` (5, new) | same face as DISPLAY | 600 | (same entry) |
| `NXUI_FONT_ITALIC` (6, new) | NXSans-Italic.ttf | 400 italic | NX Sans Italic |

`DISPLAY` and `SEMIBOLD` are the same 600-weight face. The id stays separate
so code can say what it means.

Text styles added after the existing ones (existing values are unchanged):
`NXUI_TEXT_SUBHEADLINE` (Semibold 12), `NXUI_TEXT_CALLOUT` (Regular 15),
`NXUI_TEXT_BODY_SEMIBOLD`, `NXUI_TEXT_BODY_ITALIC`, `NXUI_TEXT_DISPLAY`
(Display 40) and `NXUI_TEXT_MONO_SMALL` (Mono 11).

New text helpers: `nxui_text_line_height()`, `nxui_text_wrapped()` and
`nxui_text_wrapped_font()`. The wrap helpers break at spaces, split words
that are too long, stop at the rect bottom with an ellipsis, and measure
without drawing when the canvas is NULL.

### Fallback

`nxc_font_set_fallbacks(font, chain, n)` gives a face up to four fallback
faces. When the face has no glyph for a code point, layout uses the first
fallback that has one. Advances use that face's own units per em. Kerning
applies only between neighbouring glyphs from the same face. Only one
level is followed, so a chain cannot loop.

At startup NXUI sets every NX Sans face to fall back to NX Sans Mono, which
adds box drawing, control pictures and technical symbols. NX Sans Mono
falls back to NX Sans Regular.

A destroyed fallback face is detected by its id against the engine's
live-font table and skipped, so its memory is never read.

## Rendering pipeline and quality

For each glyph: outline, then the analytic coverage rasterizer, then the
8-bit coverage cache, then the coverage curve, then a blend into the canvas.

- **Kerning**: GPOS pair adjustment (formats 1 and 2, including extension
  lookups), resolved per first glyph when the font loads. Fonts without
  GPOS kerning use the legacy `kern` format 0 table.
- **Subpixel x positioning**: pen positions keep a phase of 1/4 px, so
  spacing and kerning stay exact. Set it with
  `nxc_text_quality_t.subpixel_phases` (1, 2 or 4).
- **Baseline snap**: baselines are always rounded to whole pixels.
- **Vertical hinting**: at sizes up to `hint_max_px` (default 24), the
  outline is scaled vertically by up to ±10% so the x-height lands on a
  whole pixel. The tops and bottoms of lowercase letters then fall on
  pixel rows instead of being split across two. Advances do not change,
  so text widths are the same with hinting on or off. Measured with the
  "grayness" metric in `test_quality.c`, hinting makes horizontal edges
  10–17% crisper at 11–17 px.
- **Coverage correction (gamma)**: blending happens in gamma-encoded
  space. Without correction, light text on dark backgrounds looks thin and
  dark text on light backgrounds looks heavy. Coverage goes through
  `cov^e`, with `e` chosen by text polarity and by one of four size classes
  (≤12, ≤16, ≤22, larger px). Light text uses e = 0.74 to 0.90; dark text
  uses e = 1.06 to 1.20.
- **Contrast**: at small sizes a smoothstep blend (strength `contrast`,
  default 0.5) sharpens anti-aliased edges.
- **Glyph cache**: entries are keyed by (font id, glyph, size in 1/8 px,
  x phase), with open addressing and an 8-slot probe window. Bitmaps live
  in a ring arena. Each entry records the monotonic ring position where its
  bitmap was written, so an entry is evicted lazily: checking it is a
  single subtraction at lookup. The old allocator scanned the whole table
  on every miss; that scan is gone. Glyphs larger than 160 px in either
  dimension are drawn straight to the canvas. `nxc_text_stats()` reports
  hits, misses, evictions, uncached glyphs and fallbacks.

Changing the quality settings with `nxc_text_set_quality()` drops the
cache. `NULL` restores the defaults.

### Measurements

Measured on the host at `-O2`: 51-glyph line, NX Sans Regular, 800×600
canvas.

| | before | after |
|--|--------|-------|
| Cached glyph draw | 0.38 µs | 0.26 µs |
| Cache miss (rasterize + insert) | 19.1 µs | 5.9 µs |
| Full character map walk (1615 code points) | n/a | 3.2 ms |

## Font information

`nxc_font_info()` fills `nxc_font_info_t` with the following:

- family and style: name IDs 16 and 17, falling back to 1 and 2
- full name, version, copyright and license text
- OS/2 weight, italic flag (fsSelection or macStyle), monospace flag
  (`post.isFixedPitch`)
- glyph count, units per em, file size, head revision and kerning kind

Name strings are decoded from UTF-16BE, with surrogate pairs handled and
lone surrogates replaced by U+FFFD, or from Mac Roman, where bytes 0x80 and
above become `?`. Control characters become spaces, the result is trimmed,
and it is cut only at a UTF-8 character boundary. A malformed `name` table
does not reject the font: the font loads with empty names and the problem
is logged. The test fuzzes this with 3000 corrupted tables.

`nxc_font_next_char(font, from)` walks the character map. Hostile cmaps do
not have to be sorted, so the walk is linear over segments and has a work
budget. `nxc_font_last_reject()` returns why the last load was rejected,
for example "table outside the file".

## Font registry

```c
nxfont_registry_init();
nxfont_registry_add_system(face, "NX Sans", "Regular", "text", &id);  /* NXUI does this */
nxfont_registry_load_user(NULL, &loaded);        /* /Library/Fonts/fonts.index */
const nxc_font_t *f = nxfont_lookup("NX Sans", 600, 0);
```

- **System fonts** are locked: they are always enabled and cannot be
  disabled or removed.
- **User fonts** live in `/Library/Fonts` (`NXFONT_USER_DIR`). They are
  listed in `fonts.index`, one entry per line:
  `<enabled 0|1> <file.ttf>`. `#` starts a comment.
- **Lookup** follows the CSS weight order. An exact weight wins. For 400 the
  next choice is 500, and for 500 it is 400. Below 400, lighter weights are
  tried first; above 500, heavier weights are tried first. The requested
  slant is preferred. Disabled faces are never returned.

### Hostile input policy

Every user font file and every index line is treated as hostile input.

- **Index file**: at most 4 KiB, read with `nx_open`/`nx_read`. Lines are
  limited to 96 bytes and the file to 256 lines. Malformed lines are logged
  and skipped.
- **File names** must be a bare name of `[A-Za-z0-9._-]`, 5–63 characters,
  not starting with a dot, ending in `.ttf`.
- **Install paths** must be absolute printable ASCII, contain no `.` or
  `..` components, no `//` and no `\`, and be shorter than 192 bytes.
- **File size** must be 256 B to 8 MiB, taken from `nx_seek(SEEK_END)`.
  Reads loop until that size is reached, a short read is rejected, and a
  read that returns more bytes than requested is rejected.
- **Structure**: `nxc_font_load` validates the table directory, every
  table range, unitsPerEm, glyph counts, every `loca` entry, the cmap
  segment and group arrays, kerning lookups and outline data before the
  face is used. It is covered by the existing fuzz tests in `test_font.c`.
- **Policy**: the font must map at least one printable code point.
  Duplicates (same family, style, version and glyph count) are refused.
  A file name that is already installed is refused. At most 24 user fonts
  are allowed.
- **Logging**: every rejection is logged with a `[FONTS]` reason. The UI
  shows the same reason.

**Install** (`nxfont_install`) runs these steps:

1. Validate the path.
2. Read and validate the file.
3. Copy it into the user directory.
4. Rewrite the index.

If the copy or the index write fails, the font stays installed for the
current session only. The returned message says so, and the entry has
`persisted = 0`.

### Kernel interface status

The registry uses only `nx_open`, `nx_read`, `nx_write`, `nx_seek` and
`nx_close`. The kernel has a `SYS_READDIR`, but `nxsyscall.h` has no wrapper
for it, and the call takes four arguments in a layout that is not mirrored
in userspace yet. Until it is, the index file is the source of truth. See
`docs/desktop/proposals/fonts.md`.

In the host preview `SYS_OPEN` returns -2, so the Font Manager shows the
honest "No user fonts installed" state and install errors such as "file
not found". The registry's file handling is tested in
`desktop/lib/nxcanvas/tests/test_registry.c` against a host directory
standing in for the VFS.

## Font Manager

Launch it from the dock, the drawer, search or the preview script with
`launch Font_Manager`. The window is 760×500.

- **Sidebar** (glass): a search field, then the families grouped as
  System and User. Each row shows "Aa" in the family's own face and the
  style count, or "Off" when every face is disabled. **Install Font…** is
  at the bottom.
- **Header**: the family name, style count and source, and the style chips
  (sorted by weight, then upright before italic). It also has the
  Specimen, Characters and Info tabs, and Disable/Enable for user fonts.
  System fonts show a "Built in" badge instead.
- **Specimen**: a sample line you can edit (click to type). The card grows
  to fit wrapped text. Below it are a size slider (8–144 px), a waterfall
  of the pangram at 12, 16, 22 and 30 px, and the alphabet and figures.
- **Characters**: a 7×7 glyph grid with paging. The arrow keys and Page
  Up/Down move the selection. The detail panel shows the glyph at 96 px on
  its metric lines (baseline, x-height, cap height, descender), its code
  point, its decimal value and UTF-8 bytes, and the glyph at text sizes.
- **Info**: the full name, family, style, weight, version and role, plus
  the glyph count, character count, kerning, file size, units per em and
  location. Below that are the license text (name 13) and the copyright.
- **Install sheet**: a path field with **Validate** (checks only) and
  **Install**. The result appears inline in green or red with the reason.

Keys: Tab cycles the tabs, Esc closes the sheet or removes focus, and Enter
confirms a field.

## Tests

The tests are in `desktop/lib/nxcanvas/tests`. Run them all with
`make all-tests`. Fonts load from `resources/fonts`; set `NXC_FONT_DIR` to
use another directory.

- `test_text.c`: layout, ellipsis and the cache. Its font path no longer
  depends on one developer's home directory.
- `test_quality.c` (new):
  - font info for all faces
  - fuzzing of the `name` table
  - walking the character map
  - fallback chains, including a destroyed fallback face under ASan
  - hinting crispness and the width invariant
  - quality setting validation
  - pixel-exact output after heavy ring eviction
  - timing
  - writes `text_quality_before_after.png`
- `test_registry.c` (new):
  - system registration and CSS-order lookup
  - install of junk, truncated, too-small, bad-offset and bad-count files
  - path traversal and bad names
  - duplicates
  - a valid install with index persistence
  - disable and enable
  - reload from a hostile index
  - refusal of an oversized index
  - checks that no file descriptor leaks
