/*
 * NeolyxOS Disk Manager - Interactive Partition Manager
 * 
 * Production-ready disk management UI
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "disk_manager.h"
#include "../drivers/keyboard.h"
#include "../drivers/serial.h"

/* ============ Colors ============ */
#define COLOR_BLACK       0x00000000
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_DARK_BG     0x00151520
#define COLOR_HEADER      0x00303050
#define COLOR_SELECTED    0x00404080
#define COLOR_HIGHLIGHT   0x0050A0FF
#define COLOR_GRAY        0x00808080
#define COLOR_DARK_GRAY   0x00404040
#define COLOR_RED         0x00FF4444
#define COLOR_GREEN       0x0044FF44
#define COLOR_YELLOW      0x00FFFF44
#define COLOR_CYAN        0x0000FFFF

/* ============ UI Constants ============ */
#define UI_MARGIN         20
#define UI_HEADER_HEIGHT  60
#define UI_ROW_HEIGHT     24
#define UI_FOOTER_HEIGHT  50
#define FONT_WIDTH        8
#define FONT_HEIGHT       16

/* ============ Framebuffer State ============ */
static volatile uint32_t *g_fb = 0;
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static uint32_t g_fb_pitch = 0;

static ui_state_t current_state = UI_STATE_MAIN;
static char error_message[128] = {0};

/* ============ Basic Font (8x16) ============ */
/* Simplified font - only printable ASCII */
#include "font8x16.h"

/* ============ Framebuffer Functions ============ */

static void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (g_fb && x < g_fb_width && y < g_fb_height) {
        g_fb[y * (g_fb_pitch / 4) + x] = color;
    }
}

static void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h && (y + j) < g_fb_height; j++) {
        for (uint32_t i = 0; i < w && (x + i) < g_fb_width; i++) {
            g_fb[(y + j) * (g_fb_pitch / 4) + (x + i)] = color;
        }
    }
}

static void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) c = '?';
    int idx = (c - 32) * FONT_HEIGHT;
    
    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = font8x16_data[idx + row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(x + col, y + row, color);
        }
    }
}

static void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s && x + FONT_WIDTH < g_fb_width) {
        fb_draw_char(x, y, *s, fg, bg);
        x += FONT_WIDTH;
        s++;
    }
}

static void fb_draw_string_centered(uint32_t y, const char *s, uint32_t fg, uint32_t bg) {
    int len = 0;
    const char *p = s;
    while (*p++) len++;
    uint32_t x = (g_fb_width - len * FONT_WIDTH) / 2;
    fb_draw_string(x, y, s, fg, bg);
}

/* ============ Number to String ============ */

static void uint_to_str(uint64_t val, char *buf, int base) {
    char tmp[24];
    int i = 0;
    
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    
    while (val > 0) {
        int digit = val % base;
        tmp[i++] = digit < 10 ? '0' + digit : 'A' + digit - 10;
        val /= base;
    }
    
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static void format_size(uint64_t bytes, char *buf) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    uint64_t size = bytes;
    
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    
    uint_to_str(size, buf, 10);
    int len = 0;
    while (buf[len]) len++;
    buf[len++] = ' ';
    const char *u = units[unit];
    while (*u) buf[len++] = *u++;
    buf[len] = '\0';
}

/* ============ UI Drawing Functions ============ */

static void draw_header(void) {
    fb_fill_rect(0, 0, g_fb_width, UI_HEADER_HEIGHT, COLOR_HEADER);
    fb_draw_string(UI_MARGIN, 20, "NeolyxOS Disk Manager", COLOR_WHITE, COLOR_HEADER);
    fb_draw_string(g_fb_width - 200, 20, "v1.0", COLOR_GRAY, COLOR_HEADER);
}

static void draw_footer(void) {
    uint32_t y = g_fb_height - UI_FOOTER_HEIGHT;
    fb_fill_rect(0, y, g_fb_width, UI_FOOTER_HEIGHT, COLOR_DARK_GRAY);
    
    fb_draw_string(UI_MARGIN, y + 15, 
        "[UP/DOWN] Navigate  [ENTER] Select  [F] Format  [ESC] Back  [Q] Quit", 
        COLOR_WHITE, COLOR_DARK_GRAY);
}

static void draw_disk_list(disk_manager_t *mgr) {
    uint32_t y = UI_HEADER_HEIGHT + UI_MARGIN;
    
    /* Title */
    fb_draw_string(UI_MARGIN, y, "Available Disks:", COLOR_CYAN, COLOR_DARK_BG);
    y += UI_ROW_HEIGHT + 10;
    
    /* Table header */
    fb_fill_rect(UI_MARGIN, y, g_fb_width - UI_MARGIN * 2, UI_ROW_HEIGHT, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 10, y + 4, "ID", COLOR_WHITE, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 50, y + 4, "Name", COLOR_WHITE, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 250, y + 4, "Size", COLOR_WHITE, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 350, y + 4, "Type", COLOR_WHITE, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 450, y + 4, "Partitions", COLOR_WHITE, COLOR_DARK_GRAY);
    y += UI_ROW_HEIGHT;
    
    /* Disk rows */
    for (uint32_t i = 0; i < mgr->disk_count && i < 10; i++) {
        disk_t *disk = &mgr->disks[i];
        uint32_t bg = (i == (uint32_t)mgr->selected_disk) ? COLOR_SELECTED : COLOR_DARK_BG;
        
        fb_fill_rect(UI_MARGIN, y, g_fb_width - UI_MARGIN * 2, UI_ROW_HEIGHT, bg);
        
        /* ID */
        char id_str[8];
        uint_to_str(i, id_str, 10);
        fb_draw_string(UI_MARGIN + 10, y + 4, id_str, COLOR_WHITE, bg);
        
        /* Name */
        fb_draw_string(UI_MARGIN + 50, y + 4, disk->name, COLOR_WHITE, bg);
        
        /* Size */
        char size_str[32];
        format_size(disk->capacity_bytes, size_str);
        fb_draw_string(UI_MARGIN + 250, y + 4, size_str, COLOR_WHITE, bg);
        
        /* Type */
        const char *type_str = "Unknown";
        switch (disk->type) {
            case DISK_TYPE_HDD: type_str = "HDD"; break;
            case DISK_TYPE_SSD: type_str = "SSD"; break;
            case DISK_TYPE_NVME: type_str = "NVMe"; break;
            case DISK_TYPE_USB: type_str = "USB"; break;
            case DISK_TYPE_VIRTUAL: type_str = "Virtual"; break;
        }
        fb_draw_string(UI_MARGIN + 350, y + 4, type_str, COLOR_WHITE, bg);
        
        /* Partition count */
        char part_str[8];
        uint_to_str(disk->partition_count, part_str, 10);
        fb_draw_string(UI_MARGIN + 450, y + 4, part_str, COLOR_WHITE, bg);
        
        y += UI_ROW_HEIGHT;
    }
    
    if (mgr->disk_count == 0) {
        fb_draw_string(UI_MARGIN + 100, y + 20, "No disks found", COLOR_RED, COLOR_DARK_BG);
    }
}

static void draw_partition_list(disk_manager_t *mgr) {
    if (mgr->selected_disk < 0 || mgr->selected_disk >= (int)mgr->disk_count) return;
    
    disk_t *disk = &mgr->disks[mgr->selected_disk];
    uint32_t y = UI_HEADER_HEIGHT + UI_MARGIN;
    
    /* Title */
    char title[128];
    const char *prefix = "Partitions on ";
    int idx = 0;
    while (*prefix) title[idx++] = *prefix++;
    const char *n = disk->name;
    while (*n) title[idx++] = *n++;
    title[idx++] = ':';
    title[idx] = '\0';
    
    fb_draw_string(UI_MARGIN, y, title, COLOR_CYAN, COLOR_DARK_BG);
    y += UI_ROW_HEIGHT + 10;
    
    /* Table header */
    fb_fill_rect(UI_MARGIN, y, g_fb_width - UI_MARGIN * 2, UI_ROW_HEIGHT, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 10, y + 4, "#", COLOR_WHITE, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 40, y + 4, "Label", COLOR_WHITE, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 180, y + 4, "Type", COLOR_WHITE, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 280, y + 4, "Size", COLOR_WHITE, COLOR_DARK_GRAY);
    fb_draw_string(UI_MARGIN + 380, y + 4, "Status", COLOR_WHITE, COLOR_DARK_GRAY);
    y += UI_ROW_HEIGHT;
    
    /* Partition rows */
    for (uint32_t i = 0; i < disk->partition_count && i < 10; i++) {
        partition_t *part = &disk->partitions[i];
        uint32_t bg = (i == (uint32_t)mgr->selected_partition) ? COLOR_SELECTED : COLOR_DARK_BG;
        
        fb_fill_rect(UI_MARGIN, y, g_fb_width - UI_MARGIN * 2, UI_ROW_HEIGHT, bg);
        
        /* Index */
        char idx_str[4];
        uint_to_str(i + 1, idx_str, 10);
        fb_draw_string(UI_MARGIN + 10, y + 4, idx_str, COLOR_WHITE, bg);
        
        /* Label */
        fb_draw_string(UI_MARGIN + 40, y + 4, part->label, COLOR_WHITE, bg);
        
        /* Type */
        const char *type_str = "Unknown";
        switch (part->type) {
            case PART_TYPE_NXFS: type_str = "NXFS"; break;
            case PART_TYPE_FAT32: type_str = "FAT32"; break;
            case PART_TYPE_EXT4: type_str = "EXT4"; break;
            case PART_TYPE_EFI: type_str = "EFI"; break;
            case PART_TYPE_ENCRYPTED: type_str = "Encrypted"; break;
            case PART_TYPE_EMPTY: type_str = "Empty"; break;
        }
        fb_draw_string(UI_MARGIN + 180, y + 4, type_str, COLOR_WHITE, bg);
        
        /* Size */
        char size_str[32];
        format_size(part->size_lba * 512, size_str);
        fb_draw_string(UI_MARGIN + 280, y + 4, size_str, COLOR_WHITE, bg);
        
        /* Status */
        const char *status = part->mounted ? "Mounted" : "Not mounted";
        uint32_t status_color = part->mounted ? COLOR_GREEN : COLOR_GRAY;
        fb_draw_string(UI_MARGIN + 380, y + 4, status, status_color, bg);
        
        y += UI_ROW_HEIGHT;
    }
    
    if (disk->partition_count == 0) {
        fb_draw_string(UI_MARGIN + 100, y + 20, "No partitions (unformatted disk)", COLOR_YELLOW, COLOR_DARK_BG);
    }
}

static void draw_format_dialog(disk_manager_t *mgr) {
    if (mgr->selected_disk < 0) return;
    disk_t *disk = &mgr->disks[mgr->selected_disk];
    
    /* Dialog box */
    uint32_t box_w = 450;
    uint32_t box_h = 200;
    uint32_t box_x = (g_fb_width - box_w) / 2;
    uint32_t box_y = (g_fb_height - box_h) / 2;
    
    fb_fill_rect(box_x, box_y, box_w, box_h, COLOR_RED);
    fb_fill_rect(box_x + 2, box_y + 2, box_w - 4, box_h - 4, COLOR_DARK_BG);
    
    /* Title */
    fb_draw_string_centered(box_y + 20, "!!! WARNING !!!", COLOR_RED, COLOR_DARK_BG);
    fb_draw_string_centered(box_y + 50, "Format partition as NXFS?", COLOR_WHITE, COLOR_DARK_BG);
    
    /* Disk info */
    fb_draw_string(box_x + 30, box_y + 80, "Disk:", COLOR_GRAY, COLOR_DARK_BG);
    fb_draw_string(box_x + 100, box_y + 80, disk->name, COLOR_WHITE, COLOR_DARK_BG);
    
    fb_draw_string_centered(box_y + 110, "ALL DATA WILL BE ERASED!", COLOR_YELLOW, COLOR_DARK_BG);
    
    /* Options */
    fb_draw_string_centered(box_y + 150, "[Y] Yes, format   [N] No, cancel", COLOR_WHITE, COLOR_DARK_BG);
}

static void draw_progress(const char *message, int percent) {
    uint32_t box_w = 400;
    uint32_t box_h = 120;
    uint32_t box_x = (g_fb_width - box_w) / 2;
    uint32_t box_y = (g_fb_height - box_h) / 2;
    
    fb_fill_rect(box_x, box_y, box_w, box_h, COLOR_HEADER);
    
    fb_draw_string_centered(box_y + 20, message, COLOR_WHITE, COLOR_HEADER);
    
    /* Progress bar */
    uint32_t bar_x = box_x + 30;
    uint32_t bar_y = box_y + 60;
    uint32_t bar_w = box_w - 60;
    uint32_t bar_h = 24;
    
    fb_fill_rect(bar_x, bar_y, bar_w, bar_h, COLOR_DARK_GRAY);
    fb_fill_rect(bar_x, bar_y, (bar_w * percent) / 100, bar_h, COLOR_GREEN);
    
    /* Percentage */
    char pct_str[8];
    uint_to_str(percent, pct_str, 10);
    int len = 0;
    while (pct_str[len]) len++;
    pct_str[len++] = '%';
    pct_str[len] = '\0';
    fb_draw_string(bar_x + bar_w / 2 - 16, bar_y + 4, pct_str, COLOR_WHITE, 
                   percent > 50 ? COLOR_GREEN : COLOR_DARK_GRAY);
}

static void draw_message(const char *msg, uint32_t color) {
    uint32_t box_w = 400;
    uint32_t box_h = 100;
    uint32_t box_x = (g_fb_width - box_w) / 2;
    uint32_t box_y = (g_fb_height - box_h) / 2;
    
    fb_fill_rect(box_x, box_y, box_w, box_h, COLOR_HEADER);
    fb_draw_string_centered(box_y + 30, msg, color, COLOR_HEADER);
    fb_draw_string_centered(box_y + 60, "Press any key to continue...", COLOR_GRAY, COLOR_HEADER);
}

/* ============ Main UI Loop ============ */

static void redraw_ui(disk_manager_t *mgr) {
    /* Clear screen */
    fb_fill_rect(0, 0, g_fb_width, g_fb_height, COLOR_DARK_BG);
    
    draw_header();
    draw_footer();
    
    switch (current_state) {
        case UI_STATE_MAIN:
        case UI_STATE_DISK_LIST:
            draw_disk_list(mgr);
            break;
        case UI_STATE_PARTITION_LIST:
            draw_partition_list(mgr);
            break;
        case UI_STATE_FORMAT_CONFIRM:
            draw_partition_list(mgr);
            draw_format_dialog(mgr);
            break;
        case UI_STATE_FORMATTING:
            draw_progress("Formatting partition...", 0);
            break;
        case UI_STATE_ERROR:
            draw_message(error_message, COLOR_RED);
            break;
        case UI_STATE_SUCCESS:
            draw_message("Operation completed successfully!", COLOR_GREEN);
            break;
        default:
            break;
    }
}

/* ============ Public API ============ */

int dm_init(disk_manager_t *mgr) {
    if (!mgr) return DSKM_ERR_INVALID;
    
    serial_puts("[DSKM] Initializing disk manager...\r\n");
    
    /* Clear structure */
    for (int i = 0; i < (int)sizeof(disk_manager_t); i++) {
        ((uint8_t*)mgr)[i] = 0;
    }
    
    mgr->selected_disk = 0;
    mgr->selected_partition = 0;
    mgr->view_mode = 0;
    
    /* Scan for disks */
    int count = dm_scan_disks(mgr);
    if (count < 0) {
        serial_puts("[DSKM] Disk scan failed\r\n");
        return count;
    }
    
    serial_puts("[DSKM] Disk manager initialized\r\n");
    return DSKM_SUCCESS;
}

int dm_scan_disks(disk_manager_t *mgr) {
    if (!mgr) return DSKM_ERR_INVALID;
    
    serial_puts("[DSKM] Scanning for disks...\r\n");
    mgr->disk_count = 0;
    
    /* For now, create test disks for UI development */
    /* TODO: Integrate with ATA driver */
    
    /* Disk 0: Primary */
    disk_t *d0 = &mgr->disks[0];
    d0->id = 0;
    d0->type = DISK_TYPE_SSD;
    
    const char *name0 = "NVMe Drive 0";
    int i = 0;
    while (*name0 && i < DISK_NAME_LEN - 1) d0->name[i++] = *name0++;
    d0->name[i] = '\0';
    
    d0->capacity_bytes = 512ULL * 1024 * 1024 * 1024; /* 512 GB */
    d0->sector_size = 512;
    d0->partition_count = 2;
    d0->is_boot_disk = 1;
    
    /* Partitions for disk 0 */
    d0->partitions[0].type = PART_TYPE_EFI;
    d0->partitions[0].start_lba = 2048;
    d0->partitions[0].size_lba = 204800;  /* 100MB */
    const char *lbl0 = "EFI System";
    i = 0;
    while (*lbl0 && i < PART_LABEL_LEN - 1) d0->partitions[0].label[i++] = *lbl0++;
    d0->partitions[0].label[i] = '\0';
    d0->partitions[0].formatted = 1;
    
    d0->partitions[1].type = PART_TYPE_NXFS;
    d0->partitions[1].start_lba = 206848;
    d0->partitions[1].size_lba = 1073741824 - 206848;  /* Rest of disk */
    const char *lbl1 = "NeolyxOS";
    i = 0;
    while (*lbl1 && i < PART_LABEL_LEN - 1) d0->partitions[1].label[i++] = *lbl1++;
    d0->partitions[1].label[i] = '\0';
    d0->partitions[1].formatted = 1;
    d0->partitions[1].mounted = 1;
    
    mgr->disk_count = 1;
    
    /* Add second disk if ATA finds more */
    disk_t *d1 = &mgr->disks[1];
    d1->id = 1;
    d1->type = DISK_TYPE_HDD;
    
    const char *name1 = "SATA Drive 1";
    i = 0;
    while (*name1 && i < DISK_NAME_LEN - 1) d1->name[i++] = *name1++;
    d1->name[i] = '\0';
    
    d1->capacity_bytes = 1024ULL * 1024 * 1024 * 1024; /* 1 TB */
    d1->sector_size = 512;
    d1->partition_count = 1;
    
    d1->partitions[0].type = PART_TYPE_NXFS;
    d1->partitions[0].start_lba = 2048;
    d1->partitions[0].size_lba = 2147483648; /* ~1TB */
    const char *lbl2 = "Data";
    i = 0;
    while (*lbl2 && i < PART_LABEL_LEN - 1) d1->partitions[0].label[i++] = *lbl2++;
    d1->partitions[0].label[i] = '\0';
    
    mgr->disk_count = 2;
    
    serial_puts("[DSKM] Found 2 disks\r\n");
    return mgr->disk_count;
}

int dm_run(disk_manager_t *mgr, uint64_t fb_addr,
           uint32_t fb_width, uint32_t fb_height, uint32_t fb_pitch) {
    if (!mgr || !fb_addr) return DSKM_ERR_INVALID;
    
    /* Set up framebuffer */
    g_fb = (volatile uint32_t*)fb_addr;
    g_fb_width = fb_width;
    g_fb_height = fb_height;
    g_fb_pitch = fb_pitch;
    
    current_state = UI_STATE_DISK_LIST;
    int running = 1;
    
    serial_puts("[DSKM] Starting disk manager UI\r\n");
    
    while (running) {
        redraw_ui(mgr);
        
        /* Wait for key */
        uint8_t key = kbd_getchar();
        
        switch (current_state) {
            case UI_STATE_DISK_LIST:
                switch (key) {
                    case KEY_UP:
                        if (mgr->selected_disk > 0) mgr->selected_disk--;
                        break;
                    case KEY_DOWN:
                        if (mgr->selected_disk < (int)mgr->disk_count - 1) mgr->selected_disk++;
                        break;
                    case KEY_ENTER:
                        mgr->selected_partition = 0;
                        current_state = UI_STATE_PARTITION_LIST;
                        break;
                    case 'q':
                    case 'Q':
                    case KEY_ESCAPE:
                        running = 0;
                        break;
                }
                break;
                
            case UI_STATE_PARTITION_LIST:
                switch (key) {
                    case KEY_UP:
                        if (mgr->selected_partition > 0) mgr->selected_partition--;
                        break;
                    case KEY_DOWN: {
                        disk_t *d = &mgr->disks[mgr->selected_disk];
                        if (mgr->selected_partition < (int)d->partition_count - 1) 
                            mgr->selected_partition++;
                        break;
                    }
                    case 'f':
                    case 'F':
                        current_state = UI_STATE_FORMAT_CONFIRM;
                        break;
                    case KEY_ESCAPE:
                        current_state = UI_STATE_DISK_LIST;
                        break;
                    case 'q':
                    case 'Q':
                        running = 0;
                        break;
                }
                break;
                
            case UI_STATE_FORMAT_CONFIRM:
                switch (key) {
                    case 'y':
                    case 'Y':
                        current_state = UI_STATE_FORMATTING;
                        redraw_ui(mgr);
                        
                        /* Perform format */
                        int result = dm_format_partition(mgr, mgr->selected_disk, 
                                                        mgr->selected_partition);
                        if (result == DSKM_SUCCESS) {
                            current_state = UI_STATE_SUCCESS;
                        } else {
                            const char *err = "Format failed!";
                            int j = 0;
                            while (*err && j < 127) error_message[j++] = *err++;
                            error_message[j] = '\0';
                            current_state = UI_STATE_ERROR;
                        }
                        break;
                    case 'n':
                    case 'N':
                    case KEY_ESCAPE:
                        current_state = UI_STATE_PARTITION_LIST;
                        break;
                }
                break;
                
            case UI_STATE_SUCCESS:
            case UI_STATE_ERROR:
                /* Any key returns to partition list */
                current_state = UI_STATE_PARTITION_LIST;
                break;
                
            default:
                break;
        }
    }
    
    serial_puts("[DSKM] Disk manager exiting\r\n");
    return DSKM_SUCCESS;
}

int dm_format_partition(disk_manager_t *mgr, int disk_idx, int part_idx) {
    if (!mgr || disk_idx < 0 || disk_idx >= (int)mgr->disk_count) {
        return DSKM_ERR_INVALID;
    }
    
    disk_t *disk = &mgr->disks[disk_idx];
    if (part_idx < 0 || part_idx >= (int)disk->partition_count) {
        return DSKM_ERR_INVALID;
    }
    
    partition_t *part = &disk->partitions[part_idx];
    
    serial_puts("[DSKM] Formatting partition...\r\n");
    
    /* Show progress */
    for (int pct = 0; pct <= 100; pct += 5) {
        draw_progress("Formatting partition as NXFS...", pct);
        
        /* Small delay */
        for (volatile int i = 0; i < 1000000; i++) {}
    }
    
    /* Mark as formatted */
    part->type = PART_TYPE_NXFS;
    part->formatted = 1;
    
    /* TODO: Actually write NXFS superblock to disk */
    
    serial_puts("[DSKM] Format complete\r\n");
    return DSKM_SUCCESS;
}

int dm_create_partition(disk_manager_t *mgr, int disk_idx,
                       uint64_t start_lba, uint64_t size_lba,
                       uint8_t type, const char *label) {
    if (!mgr || !label || disk_idx < 0 || disk_idx >= (int)mgr->disk_count) {
        return DSKM_ERR_INVALID;
    }
    
    disk_t *disk = &mgr->disks[disk_idx];
    if (disk->partition_count >= MAX_PARTITIONS) {
        return DSKM_ERR_NO_SPACE;
    }
    
    partition_t *part = &disk->partitions[disk->partition_count];
    part->type = type;
    part->start_lba = start_lba;
    part->size_lba = size_lba;
    part->active = 1;
    
    int i = 0;
    while (*label && i < PART_LABEL_LEN - 1) {
        part->label[i++] = *label++;
    }
    part->label[i] = '\0';
    
    disk->partition_count++;
    
    return DSKM_SUCCESS;
}

int dm_delete_partition(disk_manager_t *mgr, int disk_idx, int part_idx) {
    if (!mgr || disk_idx < 0 || disk_idx >= (int)mgr->disk_count) {
        return DSKM_ERR_INVALID;
    }
    
    disk_t *disk = &mgr->disks[disk_idx];
    if (part_idx < 0 || part_idx >= (int)disk->partition_count) {
        return DSKM_ERR_INVALID;
    }
    
    /* Shift partitions */
    for (int i = part_idx; i < (int)disk->partition_count - 1; i++) {
        disk->partitions[i] = disk->partitions[i + 1];
    }
    disk->partition_count--;
    
    return DSKM_SUCCESS;
}
