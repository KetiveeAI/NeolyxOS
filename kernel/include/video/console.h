/*
 * NeolyxOS Kernel - Console Header
 * 
 * Text console interface
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYXOS_CONSOLE_H
#define NEOLYXOS_CONSOLE_H

#include <stdint.h>

/* Initialize the console */
void console_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch);

/* Clear the console */
void console_clear(void);

/* Set text colors */
void console_set_color(uint32_t fg, uint32_t bg);

/* Output functions */
void console_putchar(char c);
void console_puts(const char *str);
void console_printf(const char *fmt, ...);

/* Drawing functions */
void console_draw_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void console_draw_filled_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                             uint32_t fill_color, uint32_t border_color);

/* Get dimensions */
uint32_t console_get_cols(void);
uint32_t console_get_rows(void);
uint32_t console_get_width(void);
uint32_t console_get_height(void);

#endif /* NEOLYXOS_CONSOLE_H */
