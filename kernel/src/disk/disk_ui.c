/*
 * NUCLEUS - NeolyxOS Disk Utility
 * 
 * System disk management and NXFS formatting utility
 * Launches from UEFI boot menu or desktop
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "disk_ui.h"
#include "../drivers/serial.h"

/* VGA text mode */
#define VGA_BUFFER    0xB8000
#define VGA_WIDTH     80
#define VGA_HEIGHT    25

/* NUCLEUS Color Scheme (dark blue + cyan accent) */
#define COLOR_BLACK   0x00
#define COLOR_BLUE    0x01
#define COLOR_GREEN   0x02
#define COLOR_CYAN    0x03
#define COLOR_RED     0x04
#define COLOR_WHITE   0x0F
#define COLOR_YELLOW  0x0E
#define COLOR_MAGENTA 0x05

/* NUCLEUS Theme */
#define NUCLEUS_BG        COLOR_BLUE
#define NUCLEUS_FG        COLOR_WHITE
#define NUCLEUS_ACCENT    COLOR_CYAN
#define NUCLEUS_HIGHLIGHT COLOR_YELLOW
#define NUCLEUS_DANGER    COLOR_RED

#define MAKE_COLOR(fg, bg) ((bg << 4) | fg)

static volatile uint16_t *vga = (volatile uint16_t*)VGA_BUFFER;
static int cursor_row = 0;
static int cursor_col = 0;

/* Clear screen */
static void clear_screen(uint8_t color) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = ' ' | (color << 8);
    }
    cursor_row = cursor_col = 0;
}

/* Set cursor position */
static void set_cursor(int row, int col) {
    cursor_row = row;
    cursor_col = col;
}

/* Print string at current cursor */
static void print_at(int row, int col, const char *s, uint8_t color) {
    int pos = row * VGA_WIDTH + col;
    while (*s && col < VGA_WIDTH) {
        vga[pos++] = (uint8_t)*s | (color << 8);
        s++;
        col++;
    }
}

/* Print centered */
static void print_centered(int row, const char *s, uint8_t color) {
    int len = 0;
    const char *p = s;
    while (*p++) len++;
    int col = (VGA_WIDTH - len) / 2;
    print_at(row, col, s, color);
}

/* Draw horizontal line */
static void draw_hline(int row, int start, int end, char c, uint8_t color) {
    for (int col = start; col <= end; col++) {
        vga[row * VGA_WIDTH + col] = (uint8_t)c | (color << 8);
    }
}

/* Convert number to string */
static void int_to_str(uint64_t num, char *buf) {
    char tmp[20];
    int i = 0;
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (num > 0) {
        tmp[i++] = '0' + (num % 10);
        num /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

/* Initialize NUCLEUS UI */
void disk_ui_init(void) {
    clear_screen(MAKE_COLOR(NUCLEUS_FG, NUCLEUS_BG));
}

/* Draw NUCLEUS header */
void disk_ui_draw_header(void) {
    /* Title bar - cyan accent */
    for (int col = 0; col < VGA_WIDTH; col++) {
        vga[col] = ' ' | (MAKE_COLOR(COLOR_WHITE, NUCLEUS_ACCENT) << 8);
    }
    
    /* NUCLEUS branding */
    print_at(0, 2, "[*] NUCLEUS - Disk Utility", MAKE_COLOR(COLOR_WHITE, NUCLEUS_ACCENT));
    print_at(0, VGA_WIDTH - 8, "v1.0", MAKE_COLOR(NUCLEUS_HIGHLIGHT, NUCLEUS_ACCENT));
    
    /* Subtitle */
    print_at(2, 2, "Select a disk to format with NXFS:", MAKE_COLOR(NUCLEUS_FG, NUCLEUS_BG));
}

/* Draw disk table */
void disk_ui_draw_table(disk_info_t *disks, int count, int selected) {
    int start_row = 4;
    
    /* Table header */
    print_at(start_row, 2, "+----+--------------------------------+----------+------+", MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));
    print_at(start_row + 1, 2, "| ID | Disk Name                      | Size     | Type |", MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));
    print_at(start_row + 2, 2, "+----+--------------------------------+----------+------+", MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));
    
    /* Table rows */
    for (int i = 0; i < count && i < 10; i++) {
        int row = start_row + 3 + i;
        uint8_t color = (i == selected) ? 
            MAKE_COLOR(COLOR_BLACK, COLOR_CYAN) : 
            MAKE_COLOR(COLOR_WHITE, COLOR_BLUE);
        
        /* Row background */
        for (int col = 2; col < 62; col++) {
            vga[row * VGA_WIDTH + col] = ' ' | (color << 8);
        }
        
        /* ID */
        char id_str[4];
        id_str[0] = '0' + disks[i].id;
        id_str[1] = '\0';
        print_at(row, 2, "|", color);
        print_at(row, 4, id_str, color);
        
        /* Name */
        print_at(row, 7, "|", color);
        print_at(row, 9, disks[i].name, color);
        
        /* Size */
        char size_str[16];
        int_to_str(disks[i].size_mb, size_str);
        print_at(row, 42, "|", color);
        print_at(row, 44, size_str, color);
        print_at(row, 50, "MB", color);
        
        /* Type */
        print_at(row, 53, "|", color);
        const char *type_str = "HDD";
        if (disks[i].type == DISK_TYPE_SSD) type_str = "SSD";
        else if (disks[i].type == DISK_TYPE_USB) type_str = "USB";
        else if (disks[i].type == DISK_TYPE_NVME) type_str = "NVMe";
        print_at(row, 55, type_str, color);
        print_at(row, 60, "|", color);
    }
    
    /* Table footer */
    int footer_row = start_row + 3 + count;
    print_at(footer_row, 2, "+----+--------------------------------+----------+------+", MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));
}

/* Draw footer with instructions */
void disk_ui_draw_footer(void) {
    int row = VGA_HEIGHT - 2;
    for (int col = 0; col < VGA_WIDTH; col++) {
        vga[row * VGA_WIDTH + col] = ' ' | (MAKE_COLOR(COLOR_BLACK, COLOR_WHITE) << 8);
    }
    print_at(row, 2, "[UP/DOWN] Select  [ENTER] Format  [ESC] Cancel", MAKE_COLOR(COLOR_BLACK, COLOR_WHITE));
}

/* Show format confirmation dialog */
void disk_ui_show_format_dialog(disk_info_t *disk) {
    int box_row = 8;
    int box_col = 15;
    int box_w = 50;
    int box_h = 8;
    
    /* Draw box */
    for (int r = 0; r < box_h; r++) {
        for (int c = 0; c < box_w; c++) {
            vga[(box_row + r) * VGA_WIDTH + box_col + c] = ' ' | (MAKE_COLOR(COLOR_WHITE, COLOR_RED) << 8);
        }
    }
    
    /* Border */
    print_at(box_row, box_col, "+------------------------------------------------+", MAKE_COLOR(COLOR_WHITE, COLOR_RED));
    print_at(box_row + box_h - 1, box_col, "+------------------------------------------------+", MAKE_COLOR(COLOR_WHITE, COLOR_RED));
    
    /* Content */
    print_at(box_row + 2, box_col + 2, "WARNING: Format disk as NXFS?", MAKE_COLOR(COLOR_YELLOW, COLOR_RED));
    print_at(box_row + 3, box_col + 2, "Disk:", MAKE_COLOR(COLOR_WHITE, COLOR_RED));
    print_at(box_row + 3, box_col + 8, disk->name, MAKE_COLOR(COLOR_WHITE, COLOR_RED));
    print_at(box_row + 4, box_col + 2, "ALL DATA WILL BE ERASED!", MAKE_COLOR(COLOR_YELLOW, COLOR_RED));
    print_at(box_row + 6, box_col + 2, "[Y] Yes, format  [N] No, cancel", MAKE_COLOR(COLOR_WHITE, COLOR_RED));
}

/* Show progress bar */
void disk_ui_show_progress(const char *message, int percent) {
    int row = 15;
    int bar_start = 10;
    int bar_width = 50;
    
    /* Message */
    print_at(row, 2, message, MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));
    
    /* Progress bar background */
    for (int i = 0; i < bar_width; i++) {
        vga[(row + 1) * VGA_WIDTH + bar_start + i] = ' ' | (MAKE_COLOR(COLOR_WHITE, COLOR_BLACK) << 8);
    }
    
    /* Progress bar fill */
    int fill = (bar_width * percent) / 100;
    for (int i = 0; i < fill; i++) {
        vga[(row + 1) * VGA_WIDTH + bar_start + i] = ' ' | (MAKE_COLOR(COLOR_WHITE, COLOR_GREEN) << 8);
    }
    
    /* Percentage */
    char pct_str[8];
    int_to_str(percent, pct_str);
    print_at(row + 1, bar_start + bar_width + 2, pct_str, MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));
    print_at(row + 1, bar_start + bar_width + 5, "%", MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));
}
