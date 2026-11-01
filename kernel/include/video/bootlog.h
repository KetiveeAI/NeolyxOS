/*
 * NeolyxOS Boot Log
 *
 * Keeps the most recent kernel log output (everything written through
 * serial_putc) in a fixed in-memory ring, so it can be shown on the screen
 * on machines without a serial port:
 *
 *   - Live mirror: when the loader requests verbose diagnostics (boot flag
 *     NEOLYX_BOOT_FLAG_VERBOSE), log lines are drawn on the GOP framebuffer
 *     as they are written, until the desktop takes the screen.
 *   - Crash screen: on a kernel exception or panic, the last lines are drawn
 *     on the framebuffer, whatever was on screen before.
 *
 * Text is drawn with writes only (no reads from the framebuffer, which can
 * be very slow when firmware maps it uncached) and the live mirror wraps to
 * the top instead of scrolling for the same reason.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_BOOTLOG_H
#define NEOLYX_BOOTLOG_H

#include <stdint.h>

/* Bytes of log kept in memory (oldest bytes are overwritten). */
#define BOOTLOG_RING_SIZE  65536u

/*
 * Boot flags word, passed by the UEFI loader in NeolyxBootInfo.reserved[4].
 * Valid only when the upper 32 bits hold NEOLYX_BOOT_FLAGS_MAGIC, so a loader
 * that predates it (reserved[4] = 0) means "no flags".
 *   bits 63..32  NEOLYX_BOOT_FLAGS_MAGIC ("FLGS")
 *   bits 15..8   UEFI GOP pixel format of the framebuffer (EFI enum value)
 *   bit 1        NEOLYX_BOOT_FLAG_WRITE_PROTECT: refuse all disk writes
 *   bit 0        NEOLYX_BOOT_FLAG_VERBOSE: show the boot log on screen
 */
#define NEOLYX_BOOT_FLAGS_MAGIC        0x53474C46u  /* "FLGS" */
#define NEOLYX_BOOT_FLAG_VERBOSE       (1u << 0)
#define NEOLYX_BOOT_FLAG_WRITE_PROTECT (1u << 1)
#define NEOLYX_BOOT_FLAGS_PIXFMT_SHIFT 8
#define NEOLYX_BOOT_FLAGS_PIXFMT_MASK  0xFFu

/* UEFI EFI_GRAPHICS_PIXEL_FORMAT values (UEFI spec, GOP) */
#define NEOLYX_PIXFMT_RGBX8   0u   /* PixelRedGreenBlueReserved8BitPerColor */
#define NEOLYX_PIXFMT_BGRX8   1u   /* PixelBlueGreenRedReserved8BitPerColor */
#define NEOLYX_PIXFMT_BITMASK 2u   /* PixelBitMask */
#define NEOLYX_PIXFMT_BLTONLY 3u   /* PixelBltOnly: no linear framebuffer */

/**
 * bootlog_putc - Record one byte of log output.
 *
 * Called by serial_putc for every byte. Safe from interrupt context: if the
 * screen is being drawn when an interrupt logs, the byte is still recorded
 * but not drawn.
 */
void bootlog_putc(char c);

/**
 * bootlog_attach_fb - Give the boot log a framebuffer to draw on.
 * @addr:   physical (identity-mapped) address of the framebuffer
 * @width:  pixels
 * @height: pixels
 * @pitch:  bytes per scanline, at least width * 4 (32 bpp only)
 *
 * The framebuffer must lie entirely below 4 GB, the range the kernel
 * identity-maps. Returns 0 on success, -1 on invalid arguments, -2 if the
 * framebuffer is above 4 GB. On failure nothing is ever drawn.
 */
int bootlog_attach_fb(uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch);

/**
 * bootlog_set_mirror - Start or stop drawing log lines live.
 *
 * Starting clears the screen and replays the lines already recorded.
 * Has no effect without an attached framebuffer.
 */
void bootlog_set_mirror(int on);

/** bootlog_mirror_active - Nonzero while live drawing is on. */
int bootlog_mirror_active(void);

/**
 * bootlog_show_crash - Draw a crash screen with the most recent log lines.
 * @title: short headline, e.g. "KERNEL EXCEPTION"; may be NULL
 *
 * For fatal paths only: it takes the screen unconditionally and does not
 * return the screen to its previous owner.
 */
void bootlog_show_crash(const char *title);

/**
 * bootlog_show_summary - Show a one-screen summary of the boot so far.
 * @title:    headline for the first row; may be NULL
 * @patterns: substrings; a log line is shown if it contains any of them
 * @count:    number of patterns
 *
 * Stops the live mirror, clears the screen and lists the matching lines
 * (oldest first; if there are more than fit, the newest are kept). Used
 * before the desktop takes the screen, so a single photo shows what was
 * found. Does nothing without an attached framebuffer.
 */
void bootlog_show_summary(const char *title, const char *const *patterns, uint32_t count);

/**
 * bootlog_copy - Copy the recorded log, oldest byte first.
 * @dst: destination buffer
 * @cap: size of @dst in bytes
 *
 * If more is recorded than fits, the newest @cap bytes are copied.
 * Returns the number of bytes copied (0 if @dst is NULL or @cap is 0).
 */
uint32_t bootlog_copy(char *dst, uint32_t cap);

/**
 * bootlog_boot_flags - Decode the loader's boot flags word.
 * @word: NeolyxBootInfo.reserved[4]
 * @flags_out: receives the flag bits (0 if the word is not valid)
 * @pixfmt_out: receives the GOP pixel format (NEOLYX_PIXFMT_BGRX8 if unknown)
 *
 * Returns 1 if the word carried the magic, 0 otherwise. Either out pointer
 * may be NULL.
 */
int bootlog_boot_flags(uint64_t word, uint32_t *flags_out, uint32_t *pixfmt_out);

#endif /* NEOLYX_BOOTLOG_H */
