/*
 * NeolyxOS Path.app - Status Bar
 * 
 * Bottom status bar showing item count, selection info, and messages.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"

/* ============ Colors ============ */

#define COLOR_BAR_BG        0x181825
#define COLOR_BAR_BORDER    0x313244
#define COLOR_TEXT          0xCDD6F4
#define COLOR_TEXT_DIM      0x6C7086
#define COLOR_SIZE          0x89B4FA

/* ============ Status Bar State ============ */

typedef struct {
    int item_count;
    int selected_count;
    uint64_t total_size;
    uint64_t selected_size;
    char message[128];
    int message_timeout;
    int height;
} status_bar_t;

/* ============ Initialization ============ */

void status_bar_init(status_bar_t *bar, int height) {
    bar->item_count = 0;
    bar->selected_count = 0;
    bar->total_size = 0;
    bar->selected_size = 0;
    bar->message[0] = '\0';
    bar->message_timeout = 0;
    bar->height = height;
}

/* ============ Update ============ */

void status_bar_update(status_bar_t *bar, int items, int selected, 
                       uint64_t total_size, uint64_t selected_size) {
    bar->item_count = items;
    bar->selected_count = selected;
    bar->total_size = total_size;
    bar->selected_size = selected_size;
}

void status_bar_set_message(status_bar_t *bar, const char *msg, int timeout_ms) {
    int i = 0;
    while (msg[i] && i < 127) {
        bar->message[i] = msg[i];
        i++;
    }
    bar->message[i] = '\0';
    bar->message_timeout = timeout_ms;
}

void status_bar_clear_message(status_bar_t *bar) {
    bar->message[0] = '\0';
    bar->message_timeout = 0;
}

/* ============ Size Formatting ============ */

static void format_size_short(uint64_t bytes, char *buf) {
    if (bytes < 1024) {
        int i = 0;
        if (bytes == 0) { buf[i++] = '0'; }
        else {
            char tmp[16];
            int j = 0;
            while (bytes > 0) {
                tmp[j++] = '0' + (bytes % 10);
                bytes /= 10;
            }
            while (j > 0) buf[i++] = tmp[--j];
        }
        buf[i++] = ' '; buf[i++] = 'B'; buf[i] = '\0';
    } else if (bytes < 1024 * 1024) {
        uint64_t kb = bytes / 1024;
        int i = 0;
        char tmp[16];
        int j = 0;
        while (kb > 0) {
            tmp[j++] = '0' + (kb % 10);
            kb /= 10;
        }
        while (j > 0) buf[i++] = tmp[--j];
        buf[i++] = ' '; buf[i++] = 'K'; buf[i++] = 'B'; buf[i] = '\0';
    } else if (bytes < 1024ULL * 1024 * 1024) {
        uint64_t mb = bytes / (1024 * 1024);
        int i = 0;
        char tmp[16];
        int j = 0;
        while (mb > 0) {
            tmp[j++] = '0' + (mb % 10);
            mb /= 10;
        }
        while (j > 0) buf[i++] = tmp[--j];
        buf[i++] = ' '; buf[i++] = 'M'; buf[i++] = 'B'; buf[i] = '\0';
    } else {
        uint64_t gb = bytes / (1024ULL * 1024 * 1024);
        int i = 0;
        char tmp[16];
        int j = 0;
        while (gb > 0) {
            tmp[j++] = '0' + (gb % 10);
            gb /= 10;
        }
        while (j > 0) buf[i++] = tmp[--j];
        buf[i++] = ' '; buf[i++] = 'G'; buf[i++] = 'B'; buf[i] = '\0';
    }
}

static void int_to_str(int num, char *buf) {
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    
    char tmp[16];
    int i = 0;
    int neg = 0;
    
    if (num < 0) {
        neg = 1;
        num = -num;
    }
    
    while (num > 0) {
        tmp[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* ============ Drawing ============ */

void status_bar_draw(void *ctx, status_bar_t *bar, int x, int y, int width) {
    int h = bar->height;
    int padding = 12;
    
    /* Background */
    nx_fill_rect(ctx, x, y, width, h, COLOR_BAR_BG);
    
    /* Top border */
    nx_fill_rect(ctx, x, y, width, 1, COLOR_BAR_BORDER);
    
    /* Left side: Item count */
    char count_str[64];
    int pos = 0;
    
    if (bar->selected_count > 0) {
        /* "X of Y items selected" */
        char num[16];
        int_to_str(bar->selected_count, num);
        for (int i = 0; num[i]; i++) count_str[pos++] = num[i];
        
        const char *of = " of ";
        for (int i = 0; of[i]; i++) count_str[pos++] = of[i];
        
        int_to_str(bar->item_count, num);
        for (int i = 0; num[i]; i++) count_str[pos++] = num[i];
        
        const char *items = " items selected";
        for (int i = 0; items[i]; i++) count_str[pos++] = items[i];
    } else {
        /* "Y items" */
        char num[16];
        int_to_str(bar->item_count, num);
        for (int i = 0; num[i]; i++) count_str[pos++] = num[i];
        
        const char *items = bar->item_count == 1 ? " item" : " items";
        for (int i = 0; items[i]; i++) count_str[pos++] = items[i];
    }
    count_str[pos] = '\0';
    
    nx_draw_text(ctx, count_str, x + padding, y + (h - 12) / 2, COLOR_TEXT);
    
    /* Center: Message (if any) */
    if (bar->message[0]) {
        int msg_x = x + width / 2 - 50;  /* Approximate center */
        nx_draw_text(ctx, bar->message, msg_x, y + (h - 12) / 2, COLOR_TEXT_DIM);
    }
    
    /* Right side: Size info */
    char size_str[32];
    if (bar->selected_count > 0 && bar->selected_size > 0) {
        format_size_short(bar->selected_size, size_str);
    } else if (bar->total_size > 0) {
        format_size_short(bar->total_size, size_str);
    } else {
        size_str[0] = '\0';
    }
    
    if (size_str[0]) {
        int size_x = x + width - padding - 80;  /* Right aligned */
        nx_draw_text(ctx, size_str, size_x, y + (h - 12) / 2, COLOR_SIZE);
    }
}

/* ============ Tick (for message timeout) ============ */

void status_bar_tick(status_bar_t *bar, int delta_ms) {
    if (bar->message_timeout > 0) {
        bar->message_timeout -= delta_ms;
        if (bar->message_timeout <= 0) {
            bar->message[0] = '\0';
            bar->message_timeout = 0;
        }
    }
}
