/*
 * NeolyxOS Boot Log - in-memory log ring with on-screen mirror and crash
 * screen. See include/video/bootlog.h.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "video/bootlog.h"
#include "font.h"

/* ============ State ============ */

static char     bl_ring[BOOTLOG_RING_SIZE];
static uint64_t bl_written;            /* total bytes ever recorded */

static volatile uint32_t *bl_fb;       /* NULL: nothing is ever drawn */
static uint32_t bl_width, bl_height, bl_stride;   /* stride in pixels */

static uint32_t bl_scale;              /* 1, or 2 on large screens */
static uint32_t bl_cols, bl_rows;      /* text grid */
static uint32_t bl_row, bl_col;        /* live cursor */

static int bl_mirror;                  /* live drawing on */
static volatile int bl_drawing;        /* reentrancy guard */
static int bl_utf8_skip;               /* continuation bytes left to skip */

#define BL_MARGIN   8u
#define BL_FG       0x00E0E0E0u
#define BL_BG       0x00000000u
#define BL_TITLE_FG 0x00FFFFFFu
#define BL_TITLE_BG 0x00800000u        /* dark red on BGR, dark blue on RGB */

#define BL_4GB      0x100000000ULL

/* ============ Drawing (writes only) ============ */

static void bl_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!bl_fb || x >= bl_width || y >= bl_height) return;
    if (w > bl_width - x) w = bl_width - x;
    if (h > bl_height - y) h = bl_height - y;
    for (uint32_t j = 0; j < h; j++) {
        volatile uint32_t *row = bl_fb + (uint64_t)(y + j) * bl_stride + x;
        for (uint32_t i = 0; i < w; i++) row[i] = color;
    }
}

static void bl_draw_glyph(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg) {
    if (!bl_fb || col >= bl_cols || row >= bl_rows) return;
    const uint8_t *glyph = font_get_glyph(c);
    uint32_t x0 = BL_MARGIN + col * FONT_WIDTH * bl_scale;
    uint32_t y0 = BL_MARGIN + row * FONT_HEIGHT * bl_scale;
    for (uint32_t gy = 0; gy < FONT_HEIGHT * bl_scale; gy++) {
        uint8_t bits = glyph[gy / bl_scale];
        volatile uint32_t *dst = bl_fb + (uint64_t)(y0 + gy) * bl_stride + x0;
        for (uint32_t gx = 0; gx < FONT_WIDTH * bl_scale; gx++) {
            dst[gx] = (bits & (0x80u >> (gx / bl_scale))) ? fg : bg;
        }
    }
}

static void bl_clear_row(uint32_t row, uint32_t color) {
    if (row >= bl_rows) return;
    bl_fill(0, BL_MARGIN + row * FONT_HEIGHT * bl_scale,
            bl_width, FONT_HEIGHT * bl_scale, color);
}

/*
 * Map a log byte to a drawable ASCII character, or 0 to draw nothing.
 * UTF-8 sequences (the log uses box-drawing characters) become one '?'.
 */
static char bl_printable(char ch, int *utf8_skip) {
    uint8_t c = (uint8_t)ch;
    if (*utf8_skip > 0) {
        if ((c & 0xC0) == 0x80) { (*utf8_skip)--; return 0; }
        *utf8_skip = 0;   /* malformed: treat this byte on its own */
    }
    if (c >= 0xF0) { *utf8_skip = 3; return '?'; }
    if (c >= 0xE0) { *utf8_skip = 2; return '?'; }
    if (c >= 0xC0) { *utf8_skip = 1; return '?'; }
    if (c >= 0x80) return 0;
    if (c == '\t') return ' ';
    if (c < 32 || c > 126) return 0;
    return (char)c;
}

/* Start a new live row: wrap to the top and keep one blank row after the
 * newest text so the wrap point is visible. */
static void bl_live_newline(void) {
    bl_col = 0;
    bl_row++;
    if (bl_row >= bl_rows) bl_row = 0;
    bl_clear_row(bl_row, BL_BG);
    bl_clear_row((bl_row + 1) % bl_rows, BL_BG);
}

static void bl_live_put(char ch) {
    if (ch == '\n') { bl_live_newline(); return; }
    if (ch == '\r') return;
    char c = bl_printable(ch, &bl_utf8_skip);
    if (!c) return;
    if (bl_col >= bl_cols) bl_live_newline();
    bl_draw_glyph(bl_col, bl_row, c, BL_FG, BL_BG);
    bl_col++;
}

/* ============ Ring helpers ============ */

static uint64_t bl_oldest(void) {
    return (bl_written > BOOTLOG_RING_SIZE) ? bl_written - BOOTLOG_RING_SIZE : 0;
}

static char bl_at(uint64_t pos) {
    return bl_ring[pos % BOOTLOG_RING_SIZE];
}

/* Display width of log bytes [start, end): what bl_printable would draw. */
static uint32_t bl_line_width(uint64_t start, uint64_t end) {
    uint32_t n = 0;
    int skip = 0;
    for (uint64_t p = start; p < end; p++) {
        char ch = bl_at(p);
        if (ch == '\r' || ch == '\n') continue;
        if (bl_printable(ch, &skip)) n++;
    }
    return n;
}

/*
 * Position from which the newest log text fills at most @rows screen rows,
 * counting lines longer than the screen as several rows.
 */
static uint64_t bl_find_tail(uint32_t rows) {
    uint64_t oldest = bl_oldest();
    uint64_t end = bl_written;
    uint32_t used = 0;
    if (rows == 0 || bl_cols == 0) return bl_written;
    /* A trailing newline ends the last line; it does not start an empty one */
    if (end > oldest && bl_at(end - 1) == '\n') end--;
    while (end > oldest) {
        uint64_t start = end;
        while (start > oldest && bl_at(start - 1) != '\n') start--;
        uint32_t w = bl_line_width(start, end);
        uint32_t need = (w == 0) ? 1u : (w + bl_cols - 1) / bl_cols;
        if (used + need > rows) return end + 1 <= bl_written ? end + 1 : bl_written;
        used += need;
        if (start == oldest) return oldest;
        end = start - 1;   /* step over the newline before this line */
    }
    return oldest;
}

/* Does log line [start, end) contain @pat? */
static int bl_line_has(uint64_t start, uint64_t end, const char *pat) {
    uint64_t plen = 0;
    while (pat[plen]) plen++;
    if (plen == 0 || end - start < plen) return 0;
    for (uint64_t p = start; p + plen <= end; p++) {
        uint64_t k = 0;
        while (k < plen && bl_at(p + k) == pat[k]) k++;
        if (k == plen) return 1;
    }
    return 0;
}

/* Draw @title on the title row; returns the next free column. */
static uint32_t bl_draw_title(const char *title) {
    bl_clear_row(0, BL_TITLE_BG);
    uint32_t col = 0;
    int skip = 0;
    while (title && *title && col < bl_cols) {
        char c = bl_printable(*title++, &skip);
        if (c) bl_draw_glyph(col++, 0, c, BL_TITLE_FG, BL_TITLE_BG);
    }
    return col;
}

/* ============ Public API ============ */

void bootlog_putc(char c) {
    bl_ring[bl_written % BOOTLOG_RING_SIZE] = c;
    bl_written++;

    if (!bl_mirror || !bl_fb || bl_drawing) return;
    bl_drawing = 1;
    bl_live_put(c);
    bl_drawing = 0;
}

int bootlog_attach_fb(uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch) {
    if (addr == 0 || width < 64 || height < 64 || (pitch & 3) != 0 ||
        (uint64_t)pitch < (uint64_t)width * 4u) {
        return -1;
    }
    uint64_t end = addr + (uint64_t)pitch * height;
    if (end < addr || end > BL_4GB) {
        return -2;
    }

    bl_scale  = (width >= 2560 && height >= 1440) ? 2u : 1u;
    bl_cols   = (width - 2 * BL_MARGIN) / (FONT_WIDTH * bl_scale);
    bl_rows   = (height - 2 * BL_MARGIN) / (FONT_HEIGHT * bl_scale);
    if (bl_cols == 0 || bl_rows < 4) return -1;

    bl_width  = width;
    bl_height = height;
    bl_stride = pitch / 4u;
    bl_fb     = (volatile uint32_t *)(uintptr_t)addr;
    return 0;
}

void bootlog_set_mirror(int on) {
    if (!bl_fb) { bl_mirror = 0; return; }
    if (!on) { bl_mirror = 0; return; }
    if (bl_mirror) return;

    bl_drawing = 1;
    bl_fill(0, 0, bl_width, bl_height, BL_BG);
    bl_row = 0;
    bl_col = 0;
    bl_utf8_skip = 0;
    /* Replay what was logged before the screen was available */
    for (uint64_t p = bl_find_tail(bl_rows - 1); p < bl_written; p++) {
        bl_live_put(bl_at(p));
    }
    bl_mirror = 1;
    bl_drawing = 0;
}

int bootlog_mirror_active(void) {
    return bl_mirror;
}

void bootlog_show_crash(const char *title) {
    if (!bl_fb) return;
    bl_mirror = 0;
    bl_drawing = 1;   /* stop live drawing for good */

    bl_fill(0, 0, bl_width, bl_height, BL_BG);

    /* Title row */
    uint32_t col = bl_draw_title(title ? title : "NeolyxOS stopped");
    const char *hint = "  -  take a photo of this screen";
    while (*hint && col < bl_cols) {
        bl_draw_glyph(col++, 0, *hint++, BL_TITLE_FG, BL_TITLE_BG);
    }

    /* Last lines of the log, oldest first, from row 2 */
    uint32_t avail = bl_rows - 2;
    uint32_t row = 2;
    col = 0;
    int skip = 0;
    for (uint64_t p = bl_find_tail(avail); p < bl_written && row < bl_rows; p++) {
        char ch = bl_at(p);
        if (ch == '\n') { row++; col = 0; continue; }
        if (ch == '\r') continue;
        char c = bl_printable(ch, &skip);
        if (!c) continue;
        if (col >= bl_cols) { row++; col = 0; if (row >= bl_rows) break; }
        bl_draw_glyph(col++, row, c, BL_FG, BL_BG);
    }
}

static int bl_line_matches(uint64_t start, uint64_t end,
                           const char *const *patterns, uint32_t count) {
    for (uint32_t i = 0; patterns && i < count; i++) {
        if (patterns[i] && bl_line_has(start, end, patterns[i])) return 1;
    }
    return 0;
}

void bootlog_show_summary(const char *title, const char *const *patterns, uint32_t count) {
    if (!bl_fb) return;
    bl_mirror = 0;
    bl_drawing = 1;   /* the caller is about to hand the screen over */
    bl_fill(0, 0, bl_width, bl_height, BL_BG);
    (void)bl_draw_title(title ? title : "NeolyxOS boot summary");

    /* Pass 1, newest to oldest: how far back do the matching lines fit? */
    uint32_t avail = bl_rows - 2, used = 0;
    uint64_t oldest = bl_oldest();
    uint64_t first = bl_written;
    uint64_t end = bl_written;
    if (end > oldest && bl_at(end - 1) == '\n') end--;
    while (end > oldest) {
        uint64_t start = end;
        while (start > oldest && bl_at(start - 1) != '\n') start--;
        if (bl_line_matches(start, end, patterns, count)) {
            uint32_t w = bl_line_width(start, end);
            uint32_t need = (w == 0) ? 1u : (w + bl_cols - 1) / bl_cols;
            if (used + need > avail) break;
            used += need;
            first = start;
        }
        if (start == oldest) break;
        end = start - 1;
    }

    /* Pass 2, oldest to newest: draw the matching lines from row 2 */
    uint32_t row = 2;
    uint64_t p = first;
    while (p < bl_written && row < bl_rows) {
        uint64_t line_end = p;
        while (line_end < bl_written && bl_at(line_end) != '\n') line_end++;
        if (bl_line_matches(p, line_end, patterns, count)) {
            uint32_t col = 0;
            int skip = 0;
            for (uint64_t q = p; q < line_end && row < bl_rows; q++) {
                char ch = bl_at(q);
                if (ch == '\r') continue;
                char c = bl_printable(ch, &skip);
                if (!c) continue;
                if (col >= bl_cols) { row++; col = 0; if (row >= bl_rows) break; }
                bl_draw_glyph(col++, row, c, BL_FG, BL_BG);
            }
            row++;
        }
        p = line_end + 1;
    }
}

int bootlog_boot_flags(uint64_t word, uint32_t *flags_out, uint32_t *pixfmt_out) {
    int valid = (uint32_t)(word >> 32) == NEOLYX_BOOT_FLAGS_MAGIC;
    if (flags_out) {
        *flags_out = valid ? (uint32_t)(word & 0xFFu) : 0u;
    }
    if (pixfmt_out) {
        *pixfmt_out = valid
            ? (uint32_t)((word >> NEOLYX_BOOT_FLAGS_PIXFMT_SHIFT) & NEOLYX_BOOT_FLAGS_PIXFMT_MASK)
            : NEOLYX_PIXFMT_BGRX8;
    }
    return valid;
}
