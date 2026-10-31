/*
 * NeolyxOS - NXFrame: the Neolyx app layout kit
 *
 * The parts every NeolyxOS app window is built from, so apps share one
 * recognisable layout instead of each drawing its own sidebar and cards:
 *
 *   crest      2px accent line along the top edge of the focused window
 *              (drawn by the window manager, see nxframe_crest)
 *   title      left-anchored "App / Place" title in the title bar, with
 *              the app glyph; window controls stay on the right
 *   rail       flush navigation column: flat tone, uppercase group
 *              labels, monochrome glyphs, accent bar marks the selection
 *   page title large title with the accent tick under it
 *   ledger     sections are an uppercase label plus a rule; rows are flat
 *              label / value columns (no floating rounded cards)
 *   strip      status strip at the bottom of an app: context on the
 *              left, actions on the right
 *
 * All drawing is in absolute canvas coordinates, follows the current
 * NXUI theme (light and dark) and validates its arguments: a bad canvas
 * returns NXC_ERR_INVALID_ARG, an empty or absurd rect draws nothing.
 *
 * Design notes: docs/desktop/neolyx-frame.md.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXFRAME_H
#define NXFRAME_H

#include <stdint.h>
#include "nxcanvas.h"
#include "nxui.h"

/* ============ Metrics ============ */

#define NXFRAME_CREST_H        2.0f   /* accent line on the focused window */
#define NXFRAME_RAIL_ROW_H    30.0f   /* rail item height */
#define NXFRAME_RAIL_GROUP_H  28.0f   /* rail group label block */
#define NXFRAME_RAIL_PAD      10.0f   /* rail inner horizontal padding */
#define NXFRAME_SECTION_H     34.0f   /* ledger section header block */
#define NXFRAME_ROW_H         34.0f   /* ledger row */
#define NXFRAME_STRIP_H       30.0f   /* status strip */
#define NXFRAME_LABEL_COL     0.36f   /* ledger label column, fraction of width */

/* Item states (same bits as NXGLASS_* so callers can pass either). */
#define NXFRAME_HOVER     0x01u
#define NXFRAME_PRESSED   0x02u
#define NXFRAME_FOCUSED   0x04u
#define NXFRAME_DISABLED  0x08u
#define NXFRAME_SELECTED  0x10u

/* ============ Colors ============ */

/* Flat tone of rails and strips (slightly off the window body). */
nxc_color_t nxframe_rail_tone(void);

/* ============ Window chrome (window manager) ============ */

/* Accent crest along the top of frame r (call inside the frame clip). */
nxc_status_t nxframe_crest(nxc_canvas_t *c, nxc_rect_t r);

/*
 * Left-anchored title in title bar tb: app glyph, app name (semibold)
 * and, when place is non-empty, " / place" in the secondary color.
 * right_reserve keeps the window controls clear. focused dims it when 0.
 */
nxc_status_t nxframe_title(nxc_canvas_t *c, nxc_rect_t tb, const char *app,
                           const char *place, float right_reserve, int focused);

/* ============ Rail ============ */

/* Rail background in r (flat tone + hairline on the right edge). */
nxc_status_t nxframe_rail(nxc_canvas_t *c, nxc_rect_t r);

/* Uppercase group label at the top of a NXFRAME_RAIL_GROUP_H block. */
nxc_status_t nxframe_rail_group(nxc_canvas_t *c, float x, float y, float w, const char *title);

/* Pass as sym to keep the glyph slot empty for a glyph the caller draws
 * itself, centered at (r.x + NXFRAME_GLYPH_CX, mid row). */
#define NXFRAME_SYM_SLOT  (-2)
#define NXFRAME_GLYPH_CX  21.0f

/*
 * One rail item in r. sym is an nxsym_t, NXFRAME_SYM_SLOT, or -1 for no
 * glyph; detail (may be NULL) is right aligned. state: NXFRAME_* bits.
 */
nxc_status_t nxframe_rail_item(nxc_canvas_t *c, nxc_rect_t r, int sym, const char *label,
                               const char *detail, uint32_t state);

/* ============ Page ============ */

/*
 * Page title at (x, y) in width w: title, accent tick, optional subtitle
 * (wrapped). Measures only when c is NULL. Returns the height used.
 */
float nxframe_page_title(nxc_canvas_t *c, float x, float y, float w,
                         const char *title, const char *subtitle);

/* Ledger section header: uppercase label + rule to the right edge.
 * Occupies NXFRAME_SECTION_H starting at y. */
nxc_status_t nxframe_section(nxc_canvas_t *c, float x, float y, float w, const char *title);

/* Hairline divider under a ledger row (x .. x + w at y). */
void nxframe_rule(nxc_canvas_t *c, float x, float y, float w);

/*
 * Ledger row in r: label in the label column (secondary), value left
 * aligned in the value column (primary). Either may be NULL.
 */
nxc_status_t nxframe_ledger_row(nxc_canvas_t *c, nxc_rect_t r, const char *label, const char *value);

/* Row hover/press wash for interactive ledger rows. */
nxc_status_t nxframe_row_wash(nxc_canvas_t *c, nxc_rect_t r, uint32_t state);

/* Uppercase tracked caption (column headers, labels) at a baseline.
 * Returns its width; measures only when c is NULL. */
float nxframe_caps(nxc_canvas_t *c, float x, float baseline, const char *text, nxc_color_t color);

/* ============ Status strip ============ */

/* Status strip in r: flat tone, top hairline, left text (may be NULL). */
nxc_status_t nxframe_strip(nxc_canvas_t *c, nxc_rect_t r, const char *text);

#endif /* NXFRAME_H */
