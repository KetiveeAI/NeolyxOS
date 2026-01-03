/*
 * NeolyxOS Bootloader Graphics Header
 * 
 * Copyright (c) 2025 Ketivee Organization - KetiveeAI License
 */

#ifndef NEOLYX_GRAPHICS_H
#define NEOLYX_GRAPHICS_H

#include <efi.h>

/* Colors (BGRA format) */
#define COLOR_BLACK       0x00000000
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_RED         0x000000FF
#define COLOR_GREEN       0x0000FF00
#define COLOR_BLUE        0x00FF0000
#define COLOR_CYAN        0x00FFFF00
#define COLOR_YELLOW      0x0000FFFF
#define COLOR_MAGENTA     0x00FF00FF
#define COLOR_DARK_BG     0x00151520
#define COLOR_DARK_GRAY   0x00303040
#define COLOR_LIGHT_GRAY  0x00808080
#define COLOR_NEOLYX_CYAN 0x00FFD700  /* NeolyxOS brand cyan */

/* Initialize graphics (GOP) */
EFI_STATUS graphics_init(EFI_SYSTEM_TABLE *ST);

/* Get screen dimensions */
UINT32 graphics_get_width(void);
UINT32 graphics_get_height(void);

/* Pixel operations */
void graphics_put_pixel(UINT32 x, UINT32 y, UINT32 color);
void graphics_clear(UINT32 color);

/* Shape drawing */
void graphics_fill_rect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT32 color);
void graphics_draw_rect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT32 color, UINT32 thickness);

/* Progress bar */
void graphics_draw_progress_bar(UINT32 x, UINT32 y, UINT32 w, UINT32 h,
                                 UINT32 progress, UINT32 bg_color, UINT32 fg_color);

/* Logo */
void graphics_draw_logo(void);

/* Text (basic) */
void graphics_draw_char(UINT32 x, UINT32 y, CHAR16 c, UINT32 color, UINT32 scale);
void graphics_draw_text(UINT32 x, UINT32 y, CHAR16 *text, UINT32 color, UINT32 scale);

/* Splash screen */
void show_graphical_splash(EFI_SYSTEM_TABLE *ST, CHAR16 *message, UINT32 progress);
void graphics_animate_progress(EFI_SYSTEM_TABLE *ST, UINT32 start, UINT32 end, UINT32 delay_ms);

#endif /* NEOLYX_GRAPHICS_H */