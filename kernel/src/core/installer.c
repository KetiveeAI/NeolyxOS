/*
 * NeolyxOS Installer Implementation
 * 
 * Full installation wizard with disk detection and partitioning.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "core/installer.h"
#include "core/boot_mode.h"
#include "drivers/ahci.h"
#include "mm/kheap.h"
#include "fs/vfs.h"
#include "config/nlc_config.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint32_t fb_width, fb_height;
extern volatile uint32_t *framebuffer;
extern uint8_t kbd_wait_key(void);
extern uint64_t pit_get_ticks(void);
extern const uint8_t font_basic[95][16];

/* ============ Colors ============ */

#define COLOR_BG            0x0F0F1A
#define COLOR_PANEL         0x16213E
#define COLOR_PRIMARY       0x0F3460
#define COLOR_ACCENT        0xE94560
#define COLOR_TEXT          0xFFFFFF
#define COLOR_DIM           0x888888
#define COLOR_SUCCESS       0x4CAF50
#define COLOR_DISABLED      0x333333
#define COLOR_OVERLAY       0x1A1A1A  /* Fade overlay for unsupported */

/* ============ State ============ */

static installer_state_t current_state = INSTALL_STATE_WELCOME;
static install_config_t config;
static install_plan_t install_plan;  /* New: progressive disclosure plan */
static disk_info_t disks[MAX_DISKS];
static int disk_count = 0;
static int selected_disk_idx = 0;
static int selected_intent = 0;      /* Usage profile: 0=General, 1=Developer, 2=Secure, 3=Custom */
static int selected_layout = 0;      /* Layout choice: 0=Auto, 1=Manual */
static int selected_fs = 0;          /* Filesystem: 0=NXFS, 1=FAT32, etc. */
static int encryption_enabled = 0;   /* Encryption toggle */
static int advanced_expanded = 0;    /* Advanced options panel state */
static int selected_profile = 0;     /* Legacy: Desktop/Server */

/* Boot info definition (must match minimal_main.c) */
typedef struct {
    uint32_t magic;                   /* 0x4E454F58 "NEOX" */
    uint32_t version;
    uint64_t memory_map_addr;
    uint64_t memory_map_size;
    uint64_t memory_map_desc_size;
    uint32_t memory_map_desc_version;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    uint64_t kernel_physical_base;
    uint64_t kernel_size;
    uint64_t acpi_rsdp;
    uint64_t reserved[8];
} NeolyxBootInfo;

extern NeolyxBootInfo *g_boot_info;

/* Forward declarations for NXFS block ops (functions defined after ahci_write_sector) */
static int ahci_nxfs_read_block(void *device, uint64_t block, void *buffer);
static int ahci_nxfs_write_block(void *device, uint64_t block, const void *buffer);
static uint64_t ahci_nxfs_get_blocks(void *device);
static uint32_t ahci_nxfs_get_block_size(void *device);

/* State for NXFS block I/O */
static uint64_t nxfs_part_start_lba = 0;
static int nxfs_ahci_port = 0;

/* Block ops structure for NXFS */
typedef struct {
    int (*read_block)(void *device, uint64_t block, void *buffer);
    int (*write_block)(void *device, uint64_t block, const void *buffer);
    uint64_t (*get_block_count)(void *device);
    uint32_t (*get_block_size)(void *device);
} nxfs_block_ops_t;

static nxfs_block_ops_t ahci_block_ops = {
    .read_block = ahci_nxfs_read_block,
    .write_block = ahci_nxfs_write_block,
    .get_block_count = ahci_nxfs_get_blocks,
    .get_block_size = ahci_nxfs_get_block_size
};

/* ============ Drawing Helpers ============ */

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h && j < (int)fb_height; j++) {
        if (j < 0) continue;
        for (int i = x; i < x + w && i < (int)fb_width; i++) {
            if (i >= 0) framebuffer[j * fb_width + i] = color;
        }
    }
}

/* Semi-transparent overlay */
static void draw_overlay(int x, int y, int w, int h, uint32_t color, int alpha) {
    for (int j = y; j < y + h && j < (int)fb_height; j++) {
        if (j < 0) continue;
        for (int i = x; i < x + w && i < (int)fb_width; i++) {
            if (i >= 0) {
                uint32_t bg = framebuffer[j * fb_width + i];
                int r = (((color >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * (255 - alpha)) / 255;
                int g = (((color >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * (255 - alpha)) / 255;
                int b = ((color & 0xFF) * alpha + (bg & 0xFF) * (255 - alpha)) / 255;
                framebuffer[j * fb_width + i] = (r << 16) | (g << 8) | b;
            }
        }
    }
}

static void draw_border(int x, int y, int w, int h, uint32_t color) {
    draw_rect(x, y, w, 2, color);
    draw_rect(x, y + h - 2, w, 2, color);
    draw_rect(x, y, 2, h, color);
    draw_rect(x + w - 2, y, 2, h, color);
}

static void draw_char(int x, int y, char c, uint32_t color) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font_basic[c - 32];
    
    for (int cy = 0; cy < 16; cy++) {
        uint8_t row = glyph[cy];
        for (int cx = 0; cx < 8; cx++) {
            if (row & (0x80 >> cx)) {
                int px = x + cx;
                int py = y + cy;
                if (px < fb_width && py < fb_height) {
                    framebuffer[py * fb_width + px] = color;
                }
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint32_t color) {
    while (*text) {
        draw_char(x, y, *text, color);
        x += 9; /* 8px char + 1px spacing */
        text++;
    }
}

static void draw_centered(int y, const char *text, uint32_t color) {
    int len = 0;
    const char *p = text;
    while (*p++) len++;
    int x = (fb_width - len * 8) / 2;
    draw_text(x, y, text, color);
}

static void draw_progress(int x, int y, int w, int h, int percent) {
    draw_rect(x, y, w, h, COLOR_PANEL);
    draw_border(x, y, w, h, COLOR_PRIMARY);
    int fill = (w - 4) * percent / 100;
    draw_rect(x + 2, y + 2, fill, h - 4, COLOR_ACCENT);
}

static void clear_screen(void) {
    draw_rect(0, 0, fb_width, fb_height, COLOR_BG);
}

/* ============ Disk Detection ============ */

/* Helper integer to string */
void int_to_str(uint64_t v, char* b) {
    if (v == 0) { b[0] = '0'; b[1] = 0; return; }
    char temp[32];
    int i = 0;
    while (v > 0) {
        temp[i++] = (v % 10) + '0';
        v /= 10;
    }
    int j = 0;
    while (i > 0) {
        b[j++] = temp[--i];
    }
    b[j] = 0;
}

int installer_detect_disks(disk_info_t *disk_list, int max) {
    serial_puts("[INSTALL] Detecting disks...\n");
    
    int count = 0;
    
    /* Try AHCI detection */
    ahci_drive_t ahci_drives[8];
    int ahci_count = ahci_probe_drives(ahci_drives, 8);
    
    for (int i = 0; i < ahci_count && count < max; i++) {
        disk_list[count].index = count;
        disk_list[count].port = ahci_drives[i].port;
        
        /* Copy name */
        disk_list[count].name[0] = 's';
        disk_list[count].name[1] = 'd';
        disk_list[count].name[2] = 'a' + count;
        disk_list[count].name[3] = '\0';
        
        /* Copy model */
        for (int j = 0; j < 40 && ahci_drives[i].model[j]; j++) {
            disk_list[count].model[j] = ahci_drives[i].model[j];
        }
        
        disk_list[count].size_bytes = ahci_drives[i].sectors * 512;
        
        /* Check if supported (minimum size) */
        uint64_t size_gb = disk_list[count].size_bytes / (1024 * 1024 * 1024);
        disk_list[count].supported = (size_gb >= MIN_DISK_SIZE_GB) ? 1 : 0;
        
        serial_puts("[INSTALL] Disk ");
        serial_putc('0' + count);
        serial_puts(": ");
        char sz[32];
        /* Helper to print integer without stdlib */
        extern void int_to_str(uint64_t v, char* b);
        int_to_str(size_gb, sz);
        serial_puts(sz);
        serial_puts(" GB (Min: ");
        int_to_str(MIN_DISK_SIZE_GB, sz);
        serial_puts(sz);
        serial_puts(" GB) -> Supported: ");
        serial_putc(disk_list[count].supported ? 'Y' : 'N');
        serial_puts("\n");
        
        disk_list[count].selected = 0;
        count++;
    }
    
    /* If no AHCI disks, add simulated for testing - DISABLED for production */
    if (count == 0) {
        serial_puts("[INSTALL] No AHCI disks found!\n");
    }
    
    return count;
}

/* ============ Screen: Welcome ============ */

static void screen_welcome(void) {
    clear_screen();
    
    int logo_y = fb_height / 4;
    draw_centered(logo_y, "NeolyxOS", COLOR_TEXT);
    draw_centered(logo_y + 25, "The Independent Operating System", COLOR_ACCENT);
    draw_centered(logo_y + 60, "Version 1.0", COLOR_DIM);
    
    int text_y = fb_height / 2;
    draw_centered(text_y, "Welcome to the Installation Wizard", COLOR_TEXT);
    draw_centered(text_y + 30, "This will guide you through installing NeolyxOS.", COLOR_DIM);
    
    draw_centered(fb_height - 40, "Press ENTER to continue", COLOR_DIM);
}

/* ============ Screen: User Intent (NEW - Step 1) ============ */

static void screen_intent(void) {
    clear_screen();
    
    draw_centered(50, "How do you plan to use NeolyxOS?", COLOR_TEXT);
    draw_centered(80, "This helps us configure the best experience", COLOR_DIM);
    
    int opt_w = 500;
    int opt_h = 70;
    int opt_x = (fb_width - opt_w) / 2;
    int opt_y = 130;
    int gap = 15;
    
    const char *titles[] = {"General / Personal use", "Developer / Power user", 
                            "Secure / Encrypted system", "Custom (Advanced)"};
    const char *descs[] = {"Recommended for most users", "Debug tools, larger storage", 
                           "Full disk encryption", "Full control over options"};
    uint32_t badges[] = {COLOR_SUCCESS, COLOR_DIM, COLOR_DIM, COLOR_DIM};
    
    for (int i = 0; i < 4; i++) {
        int y = opt_y + i * (opt_h + gap);
        uint32_t bg = (i == selected_intent) ? COLOR_PRIMARY : COLOR_PANEL;
        
        draw_rect(opt_x, y, opt_w, opt_h, bg);
        if (i == selected_intent) draw_border(opt_x, y, opt_w, opt_h, COLOR_ACCENT);
        
        /* Radio button indicator */
        draw_rect(opt_x + 15, y + 25, 20, 20, COLOR_BG);
        if (i == selected_intent) {
            draw_rect(opt_x + 20, y + 30, 10, 10, COLOR_ACCENT);
        }
        
        draw_text(opt_x + 50, y + 15, titles[i], COLOR_TEXT);
        draw_text(opt_x + 50, y + 40, descs[i], COLOR_DIM);
        
        if (i == 0) {
            draw_text(opt_x + opt_w - 120, y + 25, "(Recommended)", badges[i]);
        }
    }
    
    draw_centered(fb_height - 40, "UP/DOWN to select, ENTER to continue", COLOR_DIM);
}

/* ============ Screen: Layout Choice (NEW - Step 3) ============ */

static void screen_layout(void) {
    clear_screen();
    
    draw_centered(80, "How should NeolyxOS use this disk?", COLOR_TEXT);
    draw_centered(110, disks[selected_disk_idx].model, COLOR_DIM);
    
    int opt_w = 500;
    int opt_h = 120;
    int opt_x = (fb_width - opt_w) / 2;
    int opt_y = 180;
    
    /* Automatic option */
    draw_rect(opt_x, opt_y, opt_w, opt_h, (selected_layout == 0) ? COLOR_PRIMARY : COLOR_PANEL);
    if (selected_layout == 0) draw_border(opt_x, opt_y, opt_w, opt_h, COLOR_ACCENT);
    draw_text(opt_x + 20, opt_y + 15, "Automatic (Recommended)", COLOR_TEXT);
    draw_text(opt_x + 20, opt_y + 40, "GPT partition table", COLOR_DIM);
    draw_text(opt_x + 20, opt_y + 60, "EFI + Recovery + System partitions", COLOR_DIM);
    draw_text(opt_x + 20, opt_y + 80, "Sizes auto-calculated, NXFS filesystem", COLOR_DIM);
    
    /* Manual option */
    draw_rect(opt_x, opt_y + opt_h + 20, opt_w, opt_h, (selected_layout == 1) ? COLOR_PRIMARY : COLOR_PANEL);
    if (selected_layout == 1) draw_border(opt_x, opt_y + opt_h + 20, opt_w, opt_h, COLOR_ACCENT);
    draw_text(opt_x + 20, opt_y + opt_h + 35, "Manual / Custom", COLOR_TEXT);
    draw_text(opt_x + 20, opt_y + opt_h + 60, "Choose filesystem, encryption, partitions", COLOR_DIM);
    draw_text(opt_x + 20, opt_y + opt_h + 80, "For advanced users", COLOR_DIM);
    
    draw_centered(fb_height - 40, "UP/DOWN to select, ENTER to continue, ESC to go back", COLOR_DIM);
}

/* ============ Screen: Filesystem Selection (Conditional) ============ */

static void screen_filesystem(void) {
    clear_screen();
    
    draw_centered(80, "Choose filesystem for System partition", COLOR_TEXT);
    
    int opt_w = 400;
    int opt_h = 60;
    int opt_x = (fb_width - opt_w) / 2;
    int opt_y = 160;
    
    const char *fs_names[] = {"NXFS (Recommended)", "FAT32 (Compatibility)", 
                              "Encrypted NXFS"};
    
    for (int i = 0; i < 3; i++) {
        int y = opt_y + i * (opt_h + 10);
        uint32_t bg = (i == selected_fs) ? COLOR_PRIMARY : COLOR_PANEL;
        
        draw_rect(opt_x, y, opt_w, opt_h, bg);
        if (i == selected_fs) draw_border(opt_x, y, opt_w, opt_h, COLOR_ACCENT);
        draw_text(opt_x + 20, y + 20, fs_names[i], COLOR_TEXT);
    }
    
    draw_centered(fb_height - 40, "UP/DOWN to select, ENTER to continue", COLOR_DIM);
}

/* ============ Screen: Encryption Toggle (Conditional) ============ */

static void screen_encryption(void) {
    clear_screen();
    
    draw_centered(100, "Disk Encryption", COLOR_TEXT);
    draw_centered(130, "Protect your data with full disk encryption", COLOR_DIM);
    
    int box_w = 500;
    int box_h = 80;
    int box_x = (fb_width - box_w) / 2;
    int box_y = fb_height / 2 - 50;
    
    draw_rect(box_x, box_y, box_w, box_h, COLOR_PANEL);
    draw_border(box_x, box_y, box_w, box_h, COLOR_PRIMARY);
    
    /* Toggle checkbox */
    draw_rect(box_x + 20, box_y + 30, 24, 24, COLOR_BG);
    if (encryption_enabled) {
        draw_rect(box_x + 24, box_y + 34, 16, 16, COLOR_ACCENT);
    }
    
    draw_text(box_x + 60, box_y + 28, "Encrypt system disk", COLOR_TEXT);
    draw_text(box_x + 60, box_y + 50, encryption_enabled ? "Enabled - requires passphrase at boot" : 
                                                           "Disabled - faster, no passphrase needed", COLOR_DIM);
    
    if (selected_intent == USAGE_SECURE) {
        draw_centered(box_y + box_h + 30, "Encryption is required for Secure profile", COLOR_ACCENT);
    } else {
        draw_centered(box_y + box_h + 30, "Recommended for laptops and portable devices", COLOR_DIM);
    }
    
    draw_centered(fb_height - 40, "SPACE to toggle, ENTER to continue", COLOR_DIM);
}

/* ============ Screen: Advanced Options (Collapsed) ============ */

static void screen_advanced(void) {
    clear_screen();
    
    draw_centered(80, "Advanced Options", COLOR_TEXT);
    draw_centered(110, "Optional settings (defaults are recommended)", COLOR_DIM);
    
    int opt_x = fb_width / 4;
    int opt_y = 180;
    int opt_h = 35;
    
    /* Wipe disk option */
    draw_rect(opt_x, opt_y, 24, 24, COLOR_BG);
    if (install_plan.wipe_disk) draw_rect(opt_x + 4, opt_y + 4, 16, 16, COLOR_ACCENT);
    draw_text(opt_x + 40, opt_y + 5, "Secure wipe disk (slower)", COLOR_TEXT);
    draw_text(opt_x + 40, opt_y + 22, "Overwrites all data with zeros", COLOR_DIM);
    
    opt_y += opt_h + 20;
    
    /* Debug symbols */
    draw_rect(opt_x, opt_y, 24, 24, COLOR_BG);
    if (install_plan.debug_symbols) draw_rect(opt_x + 4, opt_y + 4, 16, 16, COLOR_ACCENT);
    draw_text(opt_x + 40, opt_y + 5, "Include debug symbols", COLOR_TEXT);
    
    opt_y += opt_h + 20;
    
    /* Disable recovery */
    draw_rect(opt_x, opt_y, 24, 24, COLOR_BG);
    if (install_plan.disable_recovery) draw_rect(opt_x + 4, opt_y + 4, 16, 16, COLOR_ACCENT);
    draw_text(opt_x + 40, opt_y + 5, "Skip recovery partition", COLOR_TEXT);
    
    draw_centered(fb_height - 40, "1/2/3 to toggle options, ENTER to continue", COLOR_DIM);
}

/* ============ Screen: Summary (Final Confirmation) ============ */

static void screen_summary(void) {
    clear_screen();
    
    draw_centered(60, "Installation Summary", COLOR_TEXT);
    draw_centered(90, "Please review before continuing", COLOR_DIM);
    
    int info_x = fb_width / 4;
    int info_y = 150;
    int line_h = 30;
    
    /* Disk info */
    draw_text(info_x, info_y, "Disk:", COLOR_DIM);
    draw_text(info_x + 120, info_y, disks[selected_disk_idx].model, COLOR_TEXT);
    info_y += line_h;
    
    /* Size */
    uint64_t size_gb = disks[selected_disk_idx].size_bytes / (1024*1024*1024);
    char size_str[32];
    int idx = 0;
    if (size_gb >= 100) size_str[idx++] = '0' + (size_gb / 100) % 10;
    if (size_gb >= 10) size_str[idx++] = '0' + (size_gb / 10) % 10;
    size_str[idx++] = '0' + size_gb % 10;
    size_str[idx++] = ' '; size_str[idx++] = 'G'; size_str[idx++] = 'B'; size_str[idx] = '\0';
    draw_text(info_x, info_y, "Size:", COLOR_DIM);
    draw_text(info_x + 120, info_y, size_str, COLOR_TEXT);
    info_y += line_h;
    
    /* Layout */
    draw_text(info_x, info_y, "Layout:", COLOR_DIM);
    draw_text(info_x + 120, info_y, (selected_layout == 0) ? "Automatic (GPT)" : "Manual", COLOR_TEXT);
    info_y += line_h;
    
    /* Filesystem */
    const char *fs_names[] = {"NXFS", "FAT32", "ext4", "Encrypted NXFS"};
    draw_text(info_x, info_y, "Filesystem:", COLOR_DIM);
    draw_text(info_x + 120, info_y, fs_names[selected_fs], COLOR_TEXT);
    info_y += line_h;
    
    /* Encryption */
    draw_text(info_x, info_y, "Encryption:", COLOR_DIM);
    draw_text(info_x + 120, info_y, encryption_enabled ? "Enabled" : "Disabled", 
              encryption_enabled ? COLOR_SUCCESS : COLOR_TEXT);
    info_y += line_h;
    
    /* Wipe */
    draw_text(info_x, info_y, "Wipe:", COLOR_DIM);
    draw_text(info_x + 120, info_y, install_plan.wipe_disk ? "Yes (slower)" : "No (quick)", COLOR_TEXT);
    info_y += line_h + 20;
    
    /* Warning */
    draw_rect((fb_width - 500) / 2, info_y, 500, 50, 0x442200);
    draw_centered(info_y + 15, "This will ERASE all data on the selected disk!", COLOR_ACCENT);
    
    /* Buttons */
    int btn_y = fb_height - 80;
    draw_rect(fb_width / 2 - 200, btn_y, 150, 40, COLOR_PANEL);
    draw_text(fb_width / 2 - 175, btn_y + 12, "Back", COLOR_TEXT);
    
    draw_rect(fb_width / 2 + 50, btn_y, 150, 40, COLOR_ACCENT);
    draw_text(fb_width / 2 + 75, btn_y + 12, "Install", COLOR_TEXT);
    
    draw_centered(fb_height - 25, "LEFT/RIGHT to select, ENTER to confirm", COLOR_DIM);
}

/* ============ Screen: Profile (Legacy) ============ */

static void screen_profile(void) {
    clear_screen();
    
    draw_centered(60, "Choose Your Experience", COLOR_TEXT);
    draw_centered(90, "Select how you want to use NeolyxOS", COLOR_DIM);
    
    int opt_w = 400;
    int opt_h = 100;
    int opt_x = (fb_width - opt_w) / 2;
    int opt_y = 150;
    
    /* Desktop */
    draw_rect(opt_x, opt_y, opt_w, opt_h, (selected_profile == 0) ? COLOR_PRIMARY : COLOR_PANEL);
    if (selected_profile == 0) draw_border(opt_x, opt_y, opt_w, opt_h, COLOR_ACCENT);
    draw_text(opt_x + 20, opt_y + 20, "Desktop Mode", COLOR_TEXT);
    draw_text(opt_x + 20, opt_y + 45, "Full graphical environment", COLOR_DIM);
    draw_text(opt_x + 20, opt_y + 70, "Recommended", COLOR_SUCCESS);
    
    /* Server */
    draw_rect(opt_x, opt_y + 120, opt_w, opt_h, (selected_profile == 1) ? COLOR_PRIMARY : COLOR_PANEL);
    if (selected_profile == 1) draw_border(opt_x, opt_y + 120, opt_w, opt_h, COLOR_ACCENT);
    draw_text(opt_x + 20, opt_y + 140, "Server Mode", COLOR_TEXT);
    draw_text(opt_x + 20, opt_y + 165, "Terminal-first, minimal resources", COLOR_DIM);
    draw_text(opt_x + 20, opt_y + 190, "For servers & advanced users", COLOR_DIM);
    
    draw_centered(fb_height - 40, "UP/DOWN to select, ENTER to continue", COLOR_DIM);
}

/* ============ Screen: Disk Selection ============ */

static void screen_disk(void) {
    clear_screen();
    
    draw_centered(50, "Select Installation Disk", COLOR_TEXT);
    draw_centered(80, "Choose where to install NeolyxOS", COLOR_DIM);
    
    int list_x = fb_width / 4;
    int list_y = 130;
    int item_h = 80;
    
    for (int i = 0; i < disk_count; i++) {
        int y = list_y + i * (item_h + 10);
        
        /* Draw disk item */
        uint32_t bg = (i == selected_disk_idx && disks[i].supported) ? COLOR_PRIMARY : COLOR_PANEL;
        draw_rect(list_x, y, fb_width / 2, item_h, bg);
        
        if (i == selected_disk_idx && disks[i].supported) {
            draw_border(list_x, y, fb_width / 2, item_h, COLOR_ACCENT);
        }
        
        /* Disk name and model */
        draw_text(list_x + 20, y + 15, disks[i].name, 
                  disks[i].supported ? COLOR_TEXT : COLOR_DIM);
        draw_text(list_x + 80, y + 15, disks[i].model, 
                  disks[i].supported ? COLOR_DIM : COLOR_DISABLED);
        
        /* Size */
        uint64_t size_gb = disks[i].size_bytes / (1024 * 1024 * 1024);
        char size_str[32];
        int idx = 0;
        if (size_gb >= 100) size_str[idx++] = '0' + (size_gb / 100) % 10;
        if (size_gb >= 10) size_str[idx++] = '0' + (size_gb / 10) % 10;
        size_str[idx++] = '0' + size_gb % 10;
        size_str[idx++] = ' ';
        size_str[idx++] = 'G';
        size_str[idx++] = 'B';
        size_str[idx] = '\0';
        draw_text(list_x + 20, y + 45, size_str, 
                  disks[i].supported ? COLOR_ACCENT : COLOR_DISABLED);
        
        /* Fade overlay for unsupported disks */
        if (!disks[i].supported) {
            draw_overlay(list_x, y, fb_width / 2, item_h, 0x000000, 150);
            draw_text(list_x + fb_width / 4 - 80, y + 35, "Too small (min 16GB)", COLOR_DIM);
        }
    }
    
    /* Partition info */
    draw_centered(fb_height - 120, "Partitions will be created:", COLOR_DIM);
    draw_centered(fb_height - 95, "EFI: 200MB | Recovery: 3GB | System: remaining", COLOR_DIM);
    
    draw_centered(fb_height - 40, "UP/DOWN to select, ENTER to install, ESC to go back", COLOR_DIM);
}

/* ============ Screen: Partitioning ============ */

static void screen_partition(void) {
    clear_screen();
    
    draw_centered(100, "Preparing Disk", COLOR_TEXT);
    draw_centered(130, disks[selected_disk_idx].model, COLOR_DIM);
    
    int bar_x = (fb_width - 400) / 2;
    int bar_y = fb_height / 2 - 30;
    
    /* Step 1: Create GPT partition table - REAL OPERATION */
    draw_centered(fb_height / 2 - 60, "Creating GPT partition table...", COLOR_TEXT);
    draw_progress(bar_x, bar_y, 400, 30, 10);
    
    int result = installer_create_partitions(&disks[selected_disk_idx]);
    if (result != 0) {
        draw_rect(bar_x, fb_height / 2 - 60, 400, 20, COLOR_BG);
        draw_centered(fb_height / 2 - 60, "ERROR: Partition creation failed!", COLOR_ACCENT);
        draw_centered(fb_height / 2 + 30, "Press any key to abort", COLOR_DIM);
        kbd_wait_key();
        return;
    }
    draw_progress(bar_x, bar_y, 400, 30, 50);
    
    /* Step 2: Format partitions - REAL OPERATION */
    draw_rect(bar_x, fb_height / 2 - 60, 400, 20, COLOR_BG);
    draw_centered(fb_height / 2 - 60, "Formatting partitions...", COLOR_TEXT);
    
    result = installer_format_partitions(&disks[selected_disk_idx]);
    if (result != 0) {
        draw_rect(bar_x, fb_height / 2 - 60, 400, 20, COLOR_BG);
        draw_centered(fb_height / 2 - 60, "ERROR: Format failed!", COLOR_ACCENT);
        draw_centered(fb_height / 2 + 30, "Press any key to abort", COLOR_DIM);
        kbd_wait_key();
        return;
    }
    draw_progress(bar_x, bar_y, 400, 30, 100);
    
    draw_centered(fb_height / 2 + 30, "Disk prepared successfully!", COLOR_SUCCESS);
    for (volatile int i = 0; i < 10000000; i++);  /* Brief pause for user to see success */
}

/* ============ Screen: Copying (Premium Installation Progress) ============ */

/* Draw large centered text for percentage - 3x scaled font */
static void draw_large_percent(int cx, int cy, int percent, uint32_t color) {
    char buf[8];
    int idx = 0;
    if (percent >= 100) { buf[idx++] = '1'; buf[idx++] = '0'; buf[idx++] = '0'; }
    else if (percent >= 10) { buf[idx++] = '0' + (percent / 10); buf[idx++] = '0' + (percent % 10); }
    else { buf[idx++] = '0' + percent; }
    buf[idx++] = '%';
    buf[idx] = '\0';
    
    /* Draw each character at 3x scale using font_basic */
    int scale = 3;
    int char_width = 8 * scale;   /* 24px per char */
    int char_height = 16 * scale; /* 48px height */
    int spacing = 2 * scale;      /* 6px between chars */
    int total_width = idx * (char_width + spacing) - spacing;
    int start_x = cx - total_width / 2;
    
    for (int i = 0; buf[i]; i++) {
        char c = buf[i];
        if (c < 32 || c > 126) c = '?';
        const uint8_t *glyph = font_basic[c - 32];
        
        int bx = start_x + i * (char_width + spacing);
        
        /* Draw scaled glyph */
        for (int gy = 0; gy < 16; gy++) {
            uint8_t row = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                if (row & (0x80 >> gx)) {
                    /* Draw scaled pixel as a scale x scale block */
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            int px = bx + gx * scale + sx;
                            int py = cy + gy * scale + sy;
                            if (px >= 0 && px < (int)fb_width && 
                                py >= 0 && py < (int)fb_height) {
                                framebuffer[py * fb_width + px] = color;
                            }
                        }
                    }
                }
            }
        }
    }
}

/* Draw animated progress bar with glow effect */
static void draw_premium_progress(int x, int y, int w, int h, int percent) {
    /* Background track */
    draw_rect(x, y, w, h, 0x1A1A2E);
    
    /* Border */
    draw_rect(x, y, w, 2, 0x252540);
    draw_rect(x, y + h - 2, w, 2, 0x252540);
    draw_rect(x, y, 2, h, 0x252540);
    draw_rect(x + w - 2, y, 2, h, 0x252540);
    
    /* Fill with gradient effect */
    int fill_w = (w - 8) * percent / 100;
    if (fill_w > 0) {
        /* Base fill - accent color */
        draw_rect(x + 4, y + 4, fill_w, h - 8, COLOR_ACCENT);
        
        /* Lighter highlight on top half */
        draw_rect(x + 4, y + 4, fill_w, (h - 8) / 3, 0xF05575);
    }
}

/* NeolyxOS Logo 64x64 - Stylized N shape */
#define INST_LOGO_SIZE 64
static void draw_logo(int cx, int cy) {
    int logo_x = cx - INST_LOGO_SIZE / 2;
    int logo_y = cy - INST_LOGO_SIZE / 2;
    uint32_t white = 0xFFFFFF;
    uint32_t accent = COLOR_ACCENT;
    
    /* Left vertical bar */
    for (int y = 0; y < INST_LOGO_SIZE; y++) {
        for (int x = 0; x < 12; x++) {
            int px = logo_x + x;
            int py = logo_y + y;
            if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                framebuffer[py * fb_width + px] = white;
            }
        }
    }
    
    /* Right vertical bar */
    for (int y = 0; y < INST_LOGO_SIZE; y++) {
        for (int x = INST_LOGO_SIZE - 12; x < INST_LOGO_SIZE; x++) {
            int px = logo_x + x;
            int py = logo_y + y;
            if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                framebuffer[py * fb_width + px] = white;
            }
        }
    }
    
    /* Diagonal accent line */
    for (int i = 0; i < INST_LOGO_SIZE; i++) {
        int x = 6 + (i * (INST_LOGO_SIZE - 18)) / INST_LOGO_SIZE;
        int y = i;
        for (int dx = -4; dx <= 4; dx++) {
            for (int dy = -2; dy <= 2; dy++) {
                int px = logo_x + x + dx;
                int py = logo_y + y + dy;
                if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                    framebuffer[py * fb_width + px] = accent;
                }
            }
        }
    }
}

static void screen_copying(void) {
    serial_puts("[INSTALL] Starting premium installation screen...\n");
    
    /* ============== FULL SCREEN BACKGROUND ============== */
    
    /* Dark gradient background */
    for (int y = 0; y < (int)fb_height; y++) {
        /* Subtle vertical gradient: darker at top/bottom */
        int factor = (y < (int)fb_height / 2) ? y : (fb_height - y);
        factor = factor * 32 / (fb_height / 2);
        if (factor > 31) factor = 31;
        uint32_t bg = (8 + factor / 4) << 16 | (8 + factor / 4) << 8 | (16 + factor / 2);
        for (int x = 0; x < (int)fb_width; x++) {
            framebuffer[y * fb_width + x] = bg;
        }
    }
    
    /* ============== CENTERED LOGO IMAGE ============== */
    
    int logo_cx = fb_width / 2;
    int logo_cy = fb_height / 5;
    draw_logo(logo_cx, logo_cy);
    
    /* ============== INSTALLATION STEPS ============== */
    
    const char *steps[] = {
        "Preparing disk...",
        "Copying kernel...",
        "Installing bootloader...",
        "Copying drivers...",
        "Installing system files...",
        "Configuring first boot...",
        "Finalizing installation...",
    };
    const int total_steps = 7;
    
    /* Estimated seconds per step (for time remaining) */
    const int step_times[] = {2, 3, 2, 4, 8, 3, 2};  /* ~24 seconds total */
    int total_time = 0;
    for (int i = 0; i < total_steps; i++) total_time += step_times[i];
    
    int elapsed_time = 0;
    
    /* Progress bar dimensions */
    int bar_w = fb_width * 60 / 100;  /* 60% of screen width */
    int bar_x = (fb_width - bar_w) / 2;
    int bar_y = fb_height / 2 + 40;
    int bar_h = 24;
    
    /* Percentage display position */
    int percent_y = fb_height / 2 - 50;
    
    /* Step message position */
    int step_y = bar_y + bar_h + 40;
    int time_y = step_y + 30;
    
    for (int s = 0; s < total_steps; s++) {
        int percent = (s * 100) / total_steps;
        
        /* Calculate time remaining */
        int remaining = 0;
        for (int t = s; t < total_steps; t++) remaining += step_times[t];
        
        /* Animate progress from previous to current */
        int prev_percent = (s == 0) ? 0 : ((s - 1) * 100) / total_steps;
        int next_percent = ((s + 1) * 100) / total_steps;
        
        for (int anim = prev_percent; anim <= next_percent && anim <= 100; anim++) {
            /* Clear percentage area */
            draw_rect(0, percent_y - 10, fb_width, 60, 0x0F0F1A);
            
            /* Draw large percentage */
            draw_large_percent(fb_width / 2, percent_y, anim, COLOR_ACCENT);
            
            /* Draw progress bar */
            draw_premium_progress(bar_x, bar_y, bar_w, bar_h, anim);
            
            /* Clear and draw step message */
            draw_rect(0, step_y - 5, fb_width, 25, 0x0F0F1A);
            draw_centered(step_y, steps[s], COLOR_TEXT);
            
            /* Draw time remaining */
            draw_rect(0, time_y - 5, fb_width, 25, 0x0F0F1A);
            
            /* Build time string: "~X minutes remaining" or "~X seconds remaining" */
            char time_str[64];
            if (remaining >= 60) {
                int mins = (remaining + 30) / 60;  /* Round to nearest minute */
                time_str[0] = '~';
                time_str[1] = '0' + mins;
                time_str[2] = ' ';
                time_str[3] = 'm'; time_str[4] = 'i'; time_str[5] = 'n'; time_str[6] = 'u';
                time_str[7] = 't'; time_str[8] = 'e'; time_str[9] = (mins > 1) ? 's' : ' ';
                time_str[10] = ' '; time_str[11] = 'r'; time_str[12] = 'e'; time_str[13] = 'm';
                time_str[14] = 'a'; time_str[15] = 'i'; time_str[16] = 'n'; time_str[17] = 'i';
                time_str[18] = 'n'; time_str[19] = 'g'; time_str[20] = '\0';
            } else {
                time_str[0] = '~';
                if (remaining >= 10) time_str[1] = '0' + (remaining / 10);
                else time_str[1] = ' ';
                time_str[2] = '0' + (remaining % 10);
                time_str[3] = ' '; time_str[4] = 's';  time_str[5] = 'e'; time_str[6] = 'c';
                time_str[7] = 'o'; time_str[8] = 'n'; time_str[9] = 'd'; time_str[10] = 's';
                time_str[11] = ' '; time_str[12] = 'r'; time_str[13] = 'e'; time_str[14] = 'm';
                time_str[15] = 'a'; time_str[16] = 'i'; time_str[17] = 'n'; time_str[18] = 'i';
                time_str[19] = 'n'; time_str[20] = 'g'; time_str[21] = '\0';
            }
            draw_centered(time_y, time_str, 0x888888);
            
            /* Small delay for animation */
            for (volatile int d = 0; d < 500000; d++);
        }
        
        /* REAL OPERATIONS instead of simulation */
        if (s == 1) {
            /* Step 1: "Copying kernel..." - call real file copy */
            installer_copy_files(&disks[selected_disk_idx], config.profile);
        } else {
            /* Other steps: brief delay for UI responsiveness */
            for (volatile int d = 0; d < 5000000; d++);
        }
        elapsed_time += step_times[s];
    }
    
    /* ============== COMPLETION ============== */
    
    /* Clear and show 100% */
    draw_rect(0, percent_y - 10, fb_width, 60, 0x0F0F1A);
    draw_large_percent(fb_width / 2, percent_y, 100, COLOR_SUCCESS);
    draw_premium_progress(bar_x, bar_y, bar_w, bar_h, 100);
    
    draw_rect(0, step_y - 5, fb_width, 60, 0x0F0F1A);
    draw_centered(step_y, "Installation complete!", COLOR_SUCCESS);
    draw_centered(time_y, "Your system is ready", 0x888888);
    
    serial_puts("[INSTALL] Installation screen complete\n");
    for (volatile int d = 0; d < 30000000; d++);
}

/* ============ Screen: Complete ============ */

/* Draw animated checkmark */
static void draw_checkmark(int cx, int cy, int size, uint32_t color) {
    /* Simple checkmark shape */
    for (int t = 0; t < size / 3; t++) {
        draw_rect(cx - size/3 + t, cy + t, 3, 3, color);
    }
    for (int t = 0; t < size / 2; t++) {
        draw_rect(cx - size/3 + size/3 + t, cy + size/3 - t, 3, 3, color);
    }
}

/* Animated welcome screen after installation */
static void screen_complete(void) {
    serial_puts("[INSTALL] Playing welcome animation...\n");
    
    /* === Phase 1: Fade in background === */
    for (int fade = 0; fade < 16; fade++) {
        uint32_t bg = (fade * 1) << 16 | (fade * 1) << 8 | (fade * 2);
        draw_rect(0, 0, fb_width, fb_height, bg);
        for (volatile int d = 0; d < 2000000; d++);
    }
    
    clear_screen();
    
    /* === Phase 2: Animate success circle === */
    int cx = fb_width / 2;
    int cy = fb_height / 3;
    
    /* Growing circle animation */
    for (int r = 5; r <= 60; r += 5) {
        /* Draw filled circle (approximation) */
        for (int y = -r; y <= r; y++) {
            for (int x = -r; x <= r; x++) {
                if (x*x + y*y <= r*r) {
                    int px = cx + x;
                    int py = cy + y;
                    if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                        framebuffer[py * fb_width + px] = COLOR_SUCCESS;
                    }
                }
            }
        }
        for (volatile int d = 0; d < 3000000; d++);
    }
    
    /* Draw checkmark inside */
    draw_checkmark(cx, cy, 40, COLOR_TEXT);
    for (volatile int d = 0; d < 10000000; d++);
    
    /* === Phase 3: Welcome text with typewriter effect === */
    const char *welcome = "Welcome to NeolyxOS!";
    int text_y = cy + 100;
    int start_x = (fb_width - 20 * 8) / 2;  /* Approximate center */
    
    for (int i = 0; welcome[i]; i++) {
        char single[2] = {welcome[i], '\0'};
        draw_text(start_x + i * 8, text_y, single, COLOR_TEXT);
        for (volatile int d = 0; d < 5000000; d++);
    }
    
    for (volatile int d = 0; d < 20000000; d++);
    
    /* === Phase 4: Show mode and instructions === */
    const char *mode_text = (config.profile == PROFILE_DESKTOP) ? 
                            "Desktop Mode - Full graphical experience" :
                            "Server Mode - Terminal-first, minimal";
    draw_centered(text_y + 50, mode_text, COLOR_ACCENT);
    
    for (volatile int d = 0; d < 10000000; d++);
    
    /* Pulsing "Press ENTER" text */
    for (int pulse = 0; pulse < 3; pulse++) {
        draw_centered(fb_height - 80, "Press ENTER to reboot", COLOR_DIM);
        for (volatile int d = 0; d < 15000000; d++);
        draw_rect((fb_width - 200) / 2, fb_height - 90, 200, 30, COLOR_BG);
        draw_centered(fb_height - 80, "Press ENTER to reboot", COLOR_ACCENT);
        for (volatile int d = 0; d < 15000000; d++);
    }
    
    /* Final state */
    draw_centered(fb_height - 80, "Press ENTER to reboot", COLOR_ACCENT);
    draw_centered(fb_height - 50, "Your system is ready!", COLOR_SUCCESS);
    
    serial_puts("[INSTALL] Welcome animation complete\n");
}

/* ============ Screen: Remove Installation Media ============ */

/* Draw upward arrow (eject icon) */
static void draw_eject_icon(int cx, int cy, int size, uint32_t color) {
    /* Triangle pointing up */
    for (int row = 0; row < size / 2; row++) {
        int width = row * 2 + 2;
        int start_x = cx - width / 2;
        for (int x = 0; x < width; x++) {
            int px = start_x + x;
            int py = cy - size/4 + row;
            if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                framebuffer[py * fb_width + px] = color;
            }
        }
    }
    /* Base bar */
    int bar_width = size / 2;
    int bar_height = size / 6;
    draw_rect(cx - bar_width/2, cy + size/4 - bar_height, bar_width, bar_height, color);
}

/* Animated remove media screen */
static void screen_remove_media(void) {
    serial_puts("[INSTALL] Showing remove media screen...\n");
    
    clear_screen();
    
    /* Center positions */
    int cx = fb_width / 2;
    int cy = fb_height / 3;
    
    /* === Phase 1: Draw background panel === */
    int panel_w = 500;
    int panel_h = 300;
    int panel_x = (fb_width - panel_w) / 2;
    int panel_y = (fb_height - panel_h) / 2 - 40;
    
    draw_rect(panel_x, panel_y, panel_w, panel_h, COLOR_PANEL);
    draw_border(panel_x, panel_y, panel_w, panel_h, COLOR_PRIMARY);
    
    /* === Phase 2: Animate eject icon === */
    for (int bounce = 0; bounce < 3; bounce++) {
        for (int offset = 0; offset <= 10; offset += 2) {
            /* Draw icon with bounce */
            draw_rect(cx - 40, cy - 40, 80, 80, COLOR_PANEL);  /* Clear area */
            draw_eject_icon(cx, cy - offset, 60, COLOR_ACCENT);
            for (volatile int d = 0; d < 3000000; d++);
        }
        for (int offset = 10; offset >= 0; offset -= 2) {
            draw_rect(cx - 40, cy - 40, 80, 80, COLOR_PANEL);
            draw_eject_icon(cx, cy - offset, 60, COLOR_ACCENT);
            for (volatile int d = 0; d < 3000000; d++);
        }
    }
    
    /* Final static icon */
    draw_rect(cx - 40, cy - 40, 80, 80, COLOR_PANEL);
    draw_eject_icon(cx, cy, 60, COLOR_ACCENT);
    
    /* === Phase 3: Text messages === */
    draw_centered(cy + 60, "Installation Complete!", COLOR_SUCCESS);
    
    for (volatile int d = 0; d < 10000000; d++);
    
    draw_centered(cy + 95, "Please remove the installation media", COLOR_TEXT);
    draw_centered(cy + 120, "(USB drive or CD/DVD)", COLOR_DIM);
    
    for (volatile int d = 0; d < 15000000; d++);
    
    /* === Phase 4: Pulsing instruction === */
    for (int pulse = 0; pulse < 5; pulse++) {
        draw_rect((fb_width - 350) / 2, panel_y + panel_h - 50, 350, 25, COLOR_PANEL);
        draw_centered(panel_y + panel_h - 40, "Press ENTER to restart", COLOR_DIM);
        for (volatile int d = 0; d < 10000000; d++);
        
        draw_rect((fb_width - 350) / 2, panel_y + panel_h - 50, 350, 25, COLOR_PANEL);
        draw_centered(panel_y + panel_h - 40, "Press ENTER to restart", COLOR_ACCENT);
        for (volatile int d = 0; d < 10000000; d++);
    }
    
    /* Final state */
    draw_centered(panel_y + panel_h - 40, "Press ENTER to restart", COLOR_ACCENT);
    
    serial_puts("[INSTALL] Remove media screen complete\n");
}

/* ============ Main Wizard ============ */

/* ============ Main Wizard ============ */

int installer_run_wizard(void) {
    serial_puts("[INSTALL] Starting installation wizard...\n");
    
    /* Detect disks */
    disk_count = installer_detect_disks(disks, MAX_DISKS);
    
    serial_puts("[INSTALL] Detected ");
    serial_putc('0' + disk_count);
    serial_puts(" disks\n");
    
    current_state = INSTALL_STATE_WELCOME;
    selected_profile = 0;
    selected_disk_idx = 0;
    
    /* Find first supported disk */
    for (int i = 0; i < disk_count; i++) {
        if (disks[i].supported) {
            selected_disk_idx = i;
            serial_puts("[INSTALL] Selected disk ");
            serial_putc('0' + i);
            serial_puts(" as target\n");
            break;
        }
    }
    
    serial_puts("[INSTALL] Entering wizard state machine...\n");
    
    /* MANUAL INSTALLER - User controls each step */
    serial_puts("[INSTALL] Entering wizard (MANUAL MODE)...\n");
    serial_puts("[INSTALL] Press keys to navigate: Enter=Select, Up/Down=Navigate\n");
    
    while (1) {
        switch (current_state) {
            case INSTALL_STATE_WELCOME:
                serial_puts("[INSTALL] STATE: Welcome screen\n");
                screen_welcome();
                serial_puts("[INSTALL] Press ENTER to continue...\n");
                
                /* Wait for Enter key (ignore releases) */
                {
                    uint8_t key;
                    do {
                        key = kbd_wait_key();
                    } while ((key & 0x80) || key != 0x1C);  /* Wait for Enter press */
                }
                
                serial_puts("[INSTALL] User pressed Enter\n");
                
                /* Go to intent selection (NEW progressive disclosure) */
                current_state = INSTALL_STATE_INTENT;
                break;
                
            case INSTALL_STATE_INTENT:
                serial_puts("[INSTALL] STATE: User Intent selection\n");
                screen_intent();
                
                /* Interactive intent selection (4 options) */
                {
                    uint8_t key;
                    do {
                        key = kbd_wait_key();
                        if (key & 0x80) continue;
                        
                        if (key == 0x48 && selected_intent > 0) {  /* Up */
                            selected_intent--;
                            screen_intent();
                        } else if (key == 0x50 && selected_intent < 3) {  /* Down */
                            selected_intent++;
                            screen_intent();
                        }
                    } while (key != 0x1C);
                }
                
                /* Set defaults based on intent */
                install_plan.profile = (usage_profile_t)selected_intent;
                install_plan.auto_layout = 1;  /* Default to auto */
                install_plan.fs_type = FS_NXFS;
                install_plan.encryption = (selected_intent == USAGE_SECURE) ? 1 : 0;
                install_plan.wipe_disk = 0;
                install_plan.debug_symbols = (selected_intent == USAGE_DEVELOPER) ? 1 : 0;
                install_plan.disable_recovery = 0;
                
                /* Set legacy profile for compatibility */
                config.profile = (selected_intent == USAGE_GENERAL) ? PROFILE_DESKTOP : PROFILE_SERVER;
                
                serial_puts("[INSTALL] Intent: ");
                const char *intent_names[] = {"General", "Developer", "Secure", "Custom"};
                serial_puts(intent_names[selected_intent]);
                serial_puts("\n");
                
                current_state = INSTALL_STATE_DISK;
                break;
                
            case INSTALL_STATE_DISK:
                serial_puts("[INSTALL] STATE: Disk selection\n");
                screen_disk();
                
                /* Interactive disk selection */
                {
                    uint8_t key;
                    do {
                        key = kbd_wait_key();
                        if (key & 0x80) continue;
                        
                        if (key == 0x48) {  /* Up */
                            for (int i = selected_disk_idx - 1; i >= 0; i--) {
                                if (disks[i].supported) {
                                    selected_disk_idx = i;
                                    screen_disk();
                                    break;
                                }
                            }
                        } else if (key == 0x50) {  /* Down */
                            for (int i = selected_disk_idx + 1; i < disk_count; i++) {
                                if (disks[i].supported) {
                                    selected_disk_idx = i;
                                    screen_disk();
                                    break;
                                }
                            }
                        } else if (key == 0x01) {  /* ESC - go back */
                            current_state = INSTALL_STATE_INTENT;
                            break;
                        }
                    } while (key != 0x1C && key != 0x01);
                    
                    if (key == 0x01) break;  /* Handle ESC */
                    
                    if (disks[selected_disk_idx].supported) {
                        install_plan.disk_id = selected_disk_idx;
                        config.target_disk = &disks[selected_disk_idx];
                        current_state = INSTALL_STATE_LAYOUT;
                    }
                }
                break;
                
            case INSTALL_STATE_LAYOUT:
                serial_puts("[INSTALL] STATE: Layout choice\n");
                screen_layout();
                
                {
                    uint8_t key;
                    do {
                        key = kbd_wait_key();
                        if (key & 0x80) continue;
                        
                        if (key == 0x48 && selected_layout > 0) {
                            selected_layout--;
                            screen_layout();
                        } else if (key == 0x50 && selected_layout < 1) {
                            selected_layout++;
                            screen_layout();
                        } else if (key == 0x01) {
                            current_state = INSTALL_STATE_DISK;
                            break;
                        }
                    } while (key != 0x1C && key != 0x01);
                    
                    if (key == 0x01) break;
                    
                    install_plan.auto_layout = (selected_layout == 0) ? 1 : 0;
                    
                    /* Progressive disclosure: skip to summary for Auto + General */
                    if (install_plan.auto_layout && selected_intent == USAGE_GENERAL) {
                        current_state = INSTALL_STATE_SUMMARY;
                    } else if (install_plan.auto_layout) {
                        /* Auto layout but Developer/Secure: show encryption screen */
                        if (selected_intent == USAGE_SECURE) {
                            encryption_enabled = 1;  /* Force on for Secure */
                        }
                        current_state = INSTALL_STATE_SUMMARY;
                    } else {
                        /* Manual layout: show filesystem selection */
                        current_state = INSTALL_STATE_FILESYSTEM;
                    }
                }
                break;
                
            case INSTALL_STATE_FILESYSTEM:
                serial_puts("[INSTALL] STATE: Filesystem selection\n");
                screen_filesystem();
                
                {
                    uint8_t key;
                    do {
                        key = kbd_wait_key();
                        if (key & 0x80) continue;
                        
                        if (key == 0x48 && selected_fs > 0) {
                            selected_fs--;
                            screen_filesystem();
                        } else if (key == 0x50 && selected_fs < 2) {
                            selected_fs++;
                            screen_filesystem();
                        }
                    } while (key != 0x1C);
                    
                    install_plan.fs_type = (filesystem_type_t)selected_fs;
                    current_state = INSTALL_STATE_ENCRYPTION;
                }
                break;
                
            case INSTALL_STATE_ENCRYPTION:
                serial_puts("[INSTALL] STATE: Encryption toggle\n");
                
                /* Force encryption for Secure profile */
                if (selected_intent == USAGE_SECURE) {
                    encryption_enabled = 1;
                }
                
                screen_encryption();
                
                {
                    uint8_t key;
                    do {
                        key = kbd_wait_key();
                        if (key & 0x80) continue;
                        
                        if (key == 0x39 && selected_intent != USAGE_SECURE) {  /* Space - toggle */
                            encryption_enabled = !encryption_enabled;
                            screen_encryption();
                        }
                    } while (key != 0x1C);
                    
                    install_plan.encryption = encryption_enabled;
                    current_state = INSTALL_STATE_ADVANCED;
                }
                break;
                
            case INSTALL_STATE_ADVANCED:
                serial_puts("[INSTALL] STATE: Advanced options\n");
                screen_advanced();
                
                {
                    uint8_t key;
                    do {
                        key = kbd_wait_key();
                        if (key & 0x80) continue;
                        
                        if (key == 0x02) {  /* 1 key - toggle wipe */
                            install_plan.wipe_disk = !install_plan.wipe_disk;
                            screen_advanced();
                        } else if (key == 0x03) {  /* 2 key - toggle debug */
                            install_plan.debug_symbols = !install_plan.debug_symbols;
                            screen_advanced();
                        } else if (key == 0x04) {  /* 3 key - toggle recovery */
                            install_plan.disable_recovery = !install_plan.disable_recovery;
                            screen_advanced();
                        }
                    } while (key != 0x1C);
                    
                    current_state = INSTALL_STATE_SUMMARY;
                }
                break;
                
            case INSTALL_STATE_SUMMARY:
                serial_puts("[INSTALL] STATE: Summary confirmation\n");
                screen_summary();
                
                {
                    int btn_selected = 1;  /* 0=Back, 1=Install */
                    uint8_t key;
                    do {
                        key = kbd_wait_key();
                        if (key & 0x80) continue;
                        
                        if (key == 0x4B) btn_selected = 0;  /* Left - Back */
                        else if (key == 0x4D) btn_selected = 1;  /* Right - Install */
                        
                        /* Redraw buttons to show selection */
                        int btn_y = fb_height - 80;
                        draw_rect(fb_width / 2 - 200, btn_y, 150, 40, 
                                  (btn_selected == 0) ? COLOR_ACCENT : COLOR_PANEL);
                        draw_text(fb_width / 2 - 175, btn_y + 12, "Back", COLOR_TEXT);
                        draw_rect(fb_width / 2 + 50, btn_y, 150, 40, 
                                  (btn_selected == 1) ? COLOR_ACCENT : COLOR_PANEL);
                        draw_text(fb_width / 2 + 75, btn_y + 12, "Install", COLOR_TEXT);
                        
                    } while (key != 0x1C);
                    
                    if (btn_selected == 0) {
                        /* Go back to layout */
                        current_state = INSTALL_STATE_LAYOUT;
                    } else {
                        /* Proceed to execution */
                        current_state = INSTALL_STATE_EXECUTING;
                    }
                }
                break;
                
            case INSTALL_STATE_EXECUTING:
                serial_puts("[INSTALL] STATE: Executing installation\n");
                /* Lock UI and execute installation steps */
                screen_partition();
                current_state = INSTALL_STATE_COPYING;
                break;
                
            case INSTALL_STATE_PARTITION:
                screen_partition();
                current_state = INSTALL_STATE_COPYING;
                break;
                
            case INSTALL_STATE_COPYING:
                screen_copying();
                current_state = INSTALL_STATE_COMPLETE;
                break;
                
            case INSTALL_STATE_COMPLETE:
                screen_complete();
                kbd_wait_key();
                
                /* Set boot mode */
                boot_set_mode((config.profile == PROFILE_DESKTOP) ? 
                              BOOT_MODE_DESKTOP : BOOT_MODE_SERVER);
                
                /* Write installation marker file (per roadmap.pdf) */
                {
                    /* Create the marker file */
                    vfs_create("/EFI/BOOT/instald.mrk", VFS_FILE, 0644);
                    
                    vfs_file_t *marker = vfs_open("/EFI/BOOT/instald.mrk", 0x0102 /* O_WRONLY|O_CREATE */);
                    if (marker) {
                        const char *content = "NeolyxOS Installed\n"
                                              "Edition: ";
                        vfs_write(marker, content, 29);
                        
                        const char *edition = (config.profile == PROFILE_DESKTOP) ? "Desktop\n" : "Server\n";
                        vfs_write(marker, edition, 8);
                        
                        vfs_close(marker);
                        serial_puts("[INSTALL] Wrote instald.mrk marker\n");
                    } else {
                        serial_puts("[INSTALL] Warning: Could not write marker file\n");
                    }
                }
                
                /* Write boot.nlc configuration file */
                {
                    serial_puts("[INSTALL] Writing boot.nlc configuration...\n");
                    
                    /* Use static to avoid 600KB+ stack allocation */
                    static nlc_config_t boot_cfg;
                    /* clear it manually since {0} on static is only init-time */
                    for (int i=0; i<sizeof(nlc_config_t); i++) ((char*)&boot_cfg)[i] = 0;
                    /* Set filepath directly */
                    const char *cfg_path = "/EFI/BOOT/config/boot.nlc";
                    for (int i = 0; cfg_path[i] && i < 255; i++) {
                        boot_cfg.filepath[i] = cfg_path[i];
                    }
                    
                    /* Installation section */
                    nlc_set(&boot_cfg, "installation", "installed", "true");
                    nlc_set(&boot_cfg, "installation", "installer_version", "1.0.0");
                    nlc_set(&boot_cfg, "installation", "edition", 
                            (config.profile == PROFILE_DESKTOP) ? "desktop" : "server");
                    
                    /* Disk section */
                    char port_str[8];
                    int_to_str(disks[selected_disk_idx].port, port_str);
                    nlc_set(&boot_cfg, "disk", "boot_device", disks[selected_disk_idx].name);
                    nlc_set(&boot_cfg, "disk", "boot_port", port_str);
                    nlc_set(&boot_cfg, "disk", "filesystem", "nxfs");
                    
                    /* Boot section */
                    nlc_set(&boot_cfg, "boot", "timeout", "5");
                    nlc_set(&boot_cfg, "boot", "default_entry", "neolyx_os");
                    nlc_set(&boot_cfg, "boot", "kernel_path", "/System/Kernel/kernel.bin");
                    nlc_set(&boot_cfg, "boot", "safe_mode", "false");
                    nlc_set(&boot_cfg, "boot", "verbose_boot", "false");
                    
                    /* System section */
                    nlc_set(&boot_cfg, "system", "architecture", "x86_64");
                    nlc_set(&boot_cfg, "system", "kernel_version", "1.0.0");
                    
                    /* Recovery section */
                    nlc_set(&boot_cfg, "recovery", "recovery_enabled", "true");
                    nlc_set(&boot_cfg, "recovery", "boot_failures", "0");
                    nlc_set(&boot_cfg, "recovery", "max_failures", "3");
                    
                    /* First boot flag for user creation wizard */
                    nlc_set(&boot_cfg, "installation", "first_boot", "true");
                    
                    /* Skip NLC save for now - causes page fault on FAT32 boot partition */
                    /* The neolyx.installed marker is sufficient for boot detection */
                    serial_puts("[INSTALL] Skipping boot.nlc (FAT32 limitation)...\n");
                    serial_puts("[INSTALL] Will create config on first NXFS boot\n");
                    
                    /* TODO: Move NLC config to NXFS partition after first boot */
                    /* vfs_create("/EFI/BOOT/config", VFS_DIRECTORY, 0755); */
                    /* nlc_save(&boot_cfg); */
                }
                
                /* Transition to remove media screen */
                current_state = INSTALL_STATE_REMOVE_MEDIA;
                break;
                
            case INSTALL_STATE_REMOVE_MEDIA:
                screen_remove_media();
                
                /* Wait for user to press ENTER */
                {
                    uint8_t key;
                    do {
                        key = kbd_wait_key();
                    } while ((key & 0x80) || key != 0x1C);  /* Wait for Enter press */
                }
                
                serial_puts("[INSTALL] Installation complete. Rebooting...\n");
                return 0;
                
            default:
                return -1;
        }
    }
}

int installer_check_needed(void) {
    /*
     * Stage 4 - Install State Decision (per roadmap.pdf)
     * Check for /EFI/BOOT/neolyx.installed marker file
     * If missing -> Installer Mode
     * If present -> Normal Boot
     */
    
    serial_puts("[INSTALL] Checking for installation marker...\n");
    
    /* Try to open the marker file directly via VFS
     * Marker is in root of ESP at /neolyx.installed */
    vfs_file_t *marker = vfs_open("/instald.mrk", 0x0000 /* O_RDONLY */);
    if (marker) {
        vfs_close(marker);
        serial_puts("[INSTALL] Marker found - OS is installed - Normal Boot mode\n");
        return 0;  /* Installed, no installer needed */
    }
    
    /* Try alternate path in EFI/BOOT */
    marker = vfs_open("/EFI/BOOT/instald.mrk", 0x0000);
    if (marker) {
        vfs_close(marker);
        serial_puts("[INSTALL] Marker found (in EFI/BOOT) - Normal Boot mode\n");
        return 0;
    }
    
    /* Not installed - Need to run installer */
    serial_puts("[INSTALL] Marker not found - OS not installed - Installer Mode\n");
    return 1;
}

void installer_init(void) {
    serial_puts("[INSTALL] Initialized\n");
}

/* ============ GPT Structures ============ */

/* GPT GUIDs */
static const uint8_t GPT_EFI_GUID[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

/* GPT_BASIC_DATA_GUID unused */

static const uint8_t GPT_NXFS_GUID[16] = {  /* Custom NXFS GUID */
    0x4E, 0x58, 0x46, 0x53, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

/* Boot info definition (must match minimal_main.c) */
/* Boot info definition moved to top of file */

/* Wrapper for AHCI write */
extern int ahci_write(int port, uint64_t lba, uint32_t count, const void *buffer);

static int ahci_write_sector(int port, uint64_t lba, const void *buffer) {
    return ahci_write(port, lba, 1, buffer);
}

/* Wrapper for AHCI read */
extern int ahci_read(int port, uint64_t lba, uint32_t count, void *buffer);

static int ahci_read_sector(int port, uint64_t lba, void *buffer) {
    return ahci_read(port, lba, 1, buffer);
}

/* ============ NXFS Block Ops Implementations ============ */

static int ahci_nxfs_read_block(void *device, uint64_t block, void *buffer) {
    int port = (int)(uintptr_t)device;
    uint64_t lba = nxfs_part_start_lba + (block * 8);  /* 4KB = 8 sectors */
    for (int i = 0; i < 8; i++) {
        if (ahci_read_sector(port, lba + i, (uint8_t*)buffer + i*512) != 0) {
            return -1;
        }
    }
    return 0;
}

static int ahci_nxfs_write_block(void *device, uint64_t block, const void *buffer) {
    int port = (int)(uintptr_t)device;
    uint64_t lba = nxfs_part_start_lba + (block * 8);
    for (int i = 0; i < 8; i++) {
        if (ahci_write_sector(port, lba + i, (const uint8_t*)buffer + i*512) != 0) {
            return -1;
        }
    }
    return 0;
}

static uint64_t ahci_nxfs_get_blocks(void *device) {
    (void)device;
    return 0;  /* Not used during format */
}

static uint32_t ahci_nxfs_get_block_size(void *device) {
    (void)device;
    return 4096;
}

/* CRC32 for GPT */
static uint32_t gpt_crc32(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return ~crc;
}

/* Generate pseudo-random GUID */
static void generate_guid(uint8_t *guid) {
    uint64_t seed = pit_get_ticks();
    for (int i = 0; i < 16; i++) {
        seed = seed * 1103515245 + 12345;
        guid[i] = (uint8_t)(seed >> 16);
    }
    /* Set version 4 and variant */
    guid[6] = (guid[6] & 0x0F) | 0x40;
    guid[8] = (guid[8] & 0x3F) | 0x80;
}

int installer_create_partitions(disk_info_t *disk) {
    serial_puts("[INSTALL] Creating GPT partitions on ");
    serial_puts(disk->name);
    serial_puts("...\n");
    
    uint8_t sector[512];
    uint64_t total_sectors = disk->size_bytes / 512;
    
    /* ===== 1. Protective MBR (LBA 0) ===== */
    serial_puts("[INSTALL] Writing protective MBR...\n");
    
    for (int i = 0; i < 512; i++) sector[i] = 0;
    
    /* MBR partition entry for protective partition */
    uint8_t *mbr_entry = &sector[446];
    mbr_entry[0] = 0x00;       /* Not bootable */
    mbr_entry[1] = 0x00;       /* Start head */
    mbr_entry[2] = 0x02;       /* Start sector */
    mbr_entry[3] = 0x00;       /* Start cylinder */
    mbr_entry[4] = 0xEE;       /* GPT protective type */
    mbr_entry[5] = 0xFF;       /* End head */
    mbr_entry[6] = 0xFF;       /* End sector */
    mbr_entry[7] = 0xFF;       /* End cylinder */
    mbr_entry[8] = 0x01; mbr_entry[9] = 0x00;   /* Start LBA = 1 */
    mbr_entry[10] = 0x00; mbr_entry[11] = 0x00;
    /* Size = min(total - 1, 0xFFFFFFFF) */
    uint32_t mbr_size = (total_sectors - 1 > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)(total_sectors - 1);
    sector[458] = (mbr_size) & 0xFF;
    sector[459] = (mbr_size >> 8) & 0xFF;
    sector[460] = (mbr_size >> 16) & 0xFF;
    sector[461] = (mbr_size >> 24) & 0xFF;
    
    /* MBR signature */
    sector[510] = 0x55;
    sector[511] = 0xAA;
    
    if (ahci_write_sector(disk->port, 0, sector) != 0) {
        serial_puts("[INSTALL] ERROR: Failed to write MBR\n");
        return -1;
    }
    
    /* ===== 2. GPT Header (LBA 1) ===== */
    serial_puts("[INSTALL] Writing GPT header...\n");
    
    for (int i = 0; i < 512; i++) sector[i] = 0;
    
    /* GPT signature: "EFI PART" */
    sector[0] = 'E'; sector[1] = 'F'; sector[2] = 'I'; sector[3] = ' ';
    sector[4] = 'P'; sector[5] = 'A'; sector[6] = 'R'; sector[7] = 'T';
    
    /* Revision 1.0 */
    sector[8] = 0x00; sector[9] = 0x00; sector[10] = 0x01; sector[11] = 0x00;
    
    /* Header size = 92 bytes */
    sector[12] = 92; sector[13] = 0; sector[14] = 0; sector[15] = 0;
    
    /* Header CRC32 - calculated later at offset 16-19 */
    
    /* Reserved */
    sector[20] = 0; sector[21] = 0; sector[22] = 0; sector[23] = 0;
    
    /* Current LBA = 1 */
    sector[24] = 1; sector[25] = 0; sector[26] = 0; sector[27] = 0;
    sector[28] = 0; sector[29] = 0; sector[30] = 0; sector[31] = 0;
    
    /* Backup LBA = last sector */
    uint64_t backup_lba = total_sectors - 1;
    for (int i = 0; i < 8; i++) sector[32 + i] = (backup_lba >> (i * 8)) & 0xFF;
    
    /* First usable LBA = 34 */
    sector[40] = 34; sector[41] = 0; sector[42] = 0; sector[43] = 0;
    sector[44] = 0; sector[45] = 0; sector[46] = 0; sector[47] = 0;
    
    /* Last usable LBA */
    uint64_t last_usable = total_sectors - 34;
    for (int i = 0; i < 8; i++) sector[48 + i] = (last_usable >> (i * 8)) & 0xFF;
    
    /* Disk GUID */
    generate_guid(&sector[56]);
    
    /* Partition entries start at LBA 2 */
    sector[72] = 2; sector[73] = 0; sector[74] = 0; sector[75] = 0;
    sector[76] = 0; sector[77] = 0; sector[78] = 0; sector[79] = 0;
    
    /* Number of partition entries = 128 */
    sector[80] = 128; sector[81] = 0; sector[82] = 0; sector[83] = 0;
    
    /* Size of partition entry = 128 bytes */
    sector[84] = 128; sector[85] = 0; sector[86] = 0; sector[87] = 0;
    
    /* ===== 3. Create Partition Entries ===== */
    serial_puts("[INSTALL] Creating partition entries...\n");
    
    /* Partition sizes in sectors */
    uint64_t efi_size = (200 * 1024 * 1024) / 512;        /* 200 MB */
    uint64_t recovery_size = (3ULL * 1024 * 1024 * 1024) / 512; /* 3 GB */
    
    uint64_t part1_start = 2048;  /* 1MB aligned */
    uint64_t part1_end = part1_start + efi_size - 1;
    
    uint64_t part2_start = part1_end + 1;
    while (part2_start % 2048) part2_start++;  /* Align */
    uint64_t part2_end = part2_start + recovery_size - 1;
    
    uint64_t part3_start = part2_end + 1;
    while (part3_start % 2048) part3_start++;  /* Align */
    uint64_t part3_end = last_usable;
    
    /* Allocate buffer for all partition entries (128 * 128 = 16KB = 32 sectors) */
    uint8_t part_entries[128 * 128];
    for (int i = 0; i < 128 * 128; i++) part_entries[i] = 0;
    
    /* Partition 1: EFI System Partition */
    uint8_t *p1 = &part_entries[0];
    for (int i = 0; i < 16; i++) p1[i] = GPT_EFI_GUID[i];    /* Type GUID */
    generate_guid(&p1[16]);                                   /* Unique GUID */
    for (int i = 0; i < 8; i++) p1[32 + i] = (part1_start >> (i * 8)) & 0xFF;  /* First LBA */
    for (int i = 0; i < 8; i++) p1[40 + i] = (part1_end >> (i * 8)) & 0xFF;     /* Last LBA */
    p1[48] = 0;  /* Attributes */
    /* Name: "EFI System" in UTF-16LE */
    const char *name1 = "EFI System";
    for (int i = 0; name1[i] && i < 36; i++) { p1[56 + i*2] = name1[i]; p1[57 + i*2] = 0; }
    
    /* Partition 2: Recovery (NXFS) */
    uint8_t *p2 = &part_entries[128];
    for (int i = 0; i < 16; i++) p2[i] = GPT_NXFS_GUID[i];
    generate_guid(&p2[16]);
    for (int i = 0; i < 8; i++) p2[32 + i] = (part2_start >> (i * 8)) & 0xFF;
    for (int i = 0; i < 8; i++) p2[40 + i] = (part2_end >> (i * 8)) & 0xFF;
    const char *name2 = "Recovery";
    for (int i = 0; name2[i] && i < 36; i++) { p2[56 + i*2] = name2[i]; p2[57 + i*2] = 0; }
    
    /* Partition 3: System (NXFS) */
    uint8_t *p3 = &part_entries[256];
    for (int i = 0; i < 16; i++) p3[i] = GPT_NXFS_GUID[i];
    generate_guid(&p3[16]);
    for (int i = 0; i < 8; i++) p3[32 + i] = (part3_start >> (i * 8)) & 0xFF;
    for (int i = 0; i < 8; i++) p3[40 + i] = (part3_end >> (i * 8)) & 0xFF;
    const char *name3 = "NeolyxOS";
    for (int i = 0; name3[i] && i < 36; i++) { p3[56 + i*2] = name3[i]; p3[57 + i*2] = 0; }
    
    /* Calculate partition entries CRC */
    uint32_t entries_crc = gpt_crc32(part_entries, 128 * 128);
    sector[88] = (entries_crc) & 0xFF;
    sector[89] = (entries_crc >> 8) & 0xFF;
    sector[90] = (entries_crc >> 16) & 0xFF;
    sector[91] = (entries_crc >> 24) & 0xFF;
    
    /* Calculate header CRC */
    sector[16] = 0; sector[17] = 0; sector[18] = 0; sector[19] = 0;
    uint32_t header_crc = gpt_crc32(sector, 92);
    sector[16] = (header_crc) & 0xFF;
    sector[17] = (header_crc >> 8) & 0xFF;
    sector[18] = (header_crc >> 16) & 0xFF;
    sector[19] = (header_crc >> 24) & 0xFF;
    
    /* Write GPT header */
    if (ahci_write_sector(disk->port, 1, sector) != 0) {
        serial_puts("[INSTALL] ERROR: Failed to write GPT header\n");
        return -1;
    }
    
    /* ===== 4. Write Partition Entries (LBA 2-33) ===== */
    serial_puts("[INSTALL] Writing partition entries...\n");
    
    for (int lba = 0; lba < 32; lba++) {
        if (ahci_write_sector(disk->port, 2 + lba, &part_entries[lba * 512]) != 0) {
            serial_puts("[INSTALL] ERROR: Failed to write partition entry\n");
            return -1;
        }
    }
    
    /* ===== 5. Write Backup GPT at end of disk ===== */
    serial_puts("[INSTALL] Writing backup GPT...\n");
    
    /* Backup partition entries go before backup header */
    for (int lba = 0; lba < 32; lba++) {
        if (ahci_write_sector(disk->port, total_sectors - 33 + lba, &part_entries[lba * 512]) != 0) {
            serial_puts("[INSTALL] ERROR: Failed to write backup partition entries\n");
            return -1;
        }
    }
    
    /* Modify header for backup: swap current/backup LBA */
    sector[24] = (backup_lba) & 0xFF;
    sector[25] = (backup_lba >> 8) & 0xFF;
    sector[26] = (backup_lba >> 16) & 0xFF;
    sector[27] = (backup_lba >> 24) & 0xFF;
    sector[28] = (backup_lba >> 32) & 0xFF;
    sector[29] = (backup_lba >> 40) & 0xFF;
    sector[30] = (backup_lba >> 48) & 0xFF;
    sector[31] = (backup_lba >> 56) & 0xFF;
    
    sector[32] = 1; /* Backup points to LBA 1 */
    for (int i = 1; i < 8; i++) sector[32 + i] = 0;
    
    /* Partition entries LBA for backup */
    uint64_t backup_entries = total_sectors - 33;
    for (int i = 0; i < 8; i++) sector[72 + i] = (backup_entries >> (i * 8)) & 0xFF;
    
    /* Recalculate header CRC */
    sector[16] = 0; sector[17] = 0; sector[18] = 0; sector[19] = 0;
    header_crc = gpt_crc32(sector, 92);
    sector[16] = (header_crc) & 0xFF;
    sector[17] = (header_crc >> 8) & 0xFF;
    sector[18] = (header_crc >> 16) & 0xFF;
    sector[19] = (header_crc >> 24) & 0xFF;
    
    if (ahci_write_sector(disk->port, backup_lba, sector) != 0) {
        serial_puts("[INSTALL] ERROR: Failed to write backup GPT header\n");
        return -1;
    }
    
    serial_puts("[INSTALL] GPT partition table created successfully\n");
    return 0;
}

int installer_format_partitions(disk_info_t *disk) {
    serial_puts("[INSTALL] Formatting partitions...\n");
    
    uint64_t total_sectors = disk->size_bytes / 512;
    uint64_t efi_size = (200 * 1024 * 1024) / 512;
    uint64_t recovery_size = (3ULL * 1024 * 1024 * 1024) / 512;
    
    uint64_t part1_start = 2048;
    uint64_t part2_start = part1_start + efi_size;
    while (part2_start % 2048) part2_start++;
    uint64_t part3_start = part2_start + recovery_size;
    while (part3_start % 2048) part3_start++;
    uint64_t part3_end = total_sectors - 34;
    
    /* ===== Format EFI Partition as FAT32 ===== */
    serial_puts("[INSTALL] Formatting EFI partition (FAT32)...\n");
    
    uint8_t sector[512];
    for (int i = 0; i < 512; i++) sector[i] = 0;
    
    /* FAT32 Boot Sector */
    sector[0] = 0xEB; sector[1] = 0x58; sector[2] = 0x90;  /* Jump */
    /* OEM Name */
    sector[3] = 'N'; sector[4] = 'E'; sector[5] = 'O'; sector[6] = 'L';
    sector[7] = 'Y'; sector[8] = 'X'; sector[9] = 'O'; sector[10] = 'S';
    
    /* Bytes per sector = 512 */
    sector[11] = 0x00; sector[12] = 0x02;
    /* Sectors per cluster = 8 (4KB clusters) */
    sector[13] = 8;
    /* Reserved sectors = 32 */
    sector[14] = 32; sector[15] = 0;
    /* Number of FATs = 2 */
    sector[16] = 2;
    /* Root dir entries (FAT32 = 0) */
    sector[17] = 0; sector[18] = 0;
    /* Total sectors (0 = use 32-bit field) */
    sector[19] = 0; sector[20] = 0;
    /* Media type */
    sector[21] = 0xF8;
    /* FAT size (FAT16, 0 for FAT32) */
    sector[22] = 0; sector[23] = 0;
    /* Sectors per track */
    sector[24] = 0x3F; sector[25] = 0;
    /* Number of heads */
    sector[26] = 0xFF; sector[27] = 0;
    /* Hidden sectors */
    sector[28] = (part1_start) & 0xFF;
    sector[29] = (part1_start >> 8) & 0xFF;
    sector[30] = (part1_start >> 16) & 0xFF;
    sector[31] = (part1_start >> 24) & 0xFF;
    /* Total sectors 32-bit */
    sector[32] = (efi_size) & 0xFF;
    sector[33] = (efi_size >> 8) & 0xFF;
    sector[34] = (efi_size >> 16) & 0xFF;
    sector[35] = (efi_size >> 24) & 0xFF;
    /* FAT32: Sectors per FAT */
    uint32_t fat_size = (efi_size / 8 + 127) / 128;
    sector[36] = (fat_size) & 0xFF;
    sector[37] = (fat_size >> 8) & 0xFF;
    sector[38] = (fat_size >> 16) & 0xFF;
    sector[39] = (fat_size >> 24) & 0xFF;
    /* Flags */
    sector[40] = 0; sector[41] = 0;
    /* Version */
    sector[42] = 0; sector[43] = 0;
    /* Root cluster = 2 */
    sector[44] = 2; sector[45] = 0; sector[46] = 0; sector[47] = 0;
    /* FSInfo sector = 1 */
    sector[48] = 1; sector[49] = 0;
    /* Backup boot sector = 6 */
    sector[50] = 6; sector[51] = 0;
    /* Reserved */
    for (int i = 52; i < 64; i++) sector[i] = 0;
    /* Drive number */
    sector[64] = 0x80;
    /* Reserved */
    sector[65] = 0;
    /* Boot signature */
    sector[66] = 0x29;
    /* Volume ID */
    uint32_t vol_id = (uint32_t)pit_get_ticks();
    sector[67] = (vol_id) & 0xFF;
    sector[68] = (vol_id >> 8) & 0xFF;
    sector[69] = (vol_id >> 16) & 0xFF;
    sector[70] = (vol_id >> 24) & 0xFF;
    /* Volume label */
    const char *label = "EFI       ";
    for (int i = 0; i < 11; i++) sector[71 + i] = label[i];
    /* FS Type */
    sector[82] = 'F'; sector[83] = 'A'; sector[84] = 'T';
    sector[85] = '3'; sector[86] = '2'; sector[87] = ' ';
    sector[88] = ' '; sector[89] = ' ';
    /* Boot signature */
    sector[510] = 0x55; sector[511] = 0xAA;
    
    if (ahci_write_sector(disk->port, part1_start, sector) != 0) {
        serial_puts("[INSTALL] ERROR: Failed to write FAT32 boot sector\n");
        return -1;
    }
    
    /* Write FAT32 FSInfo sector */
    for (int i = 0; i < 512; i++) sector[i] = 0;
    sector[0] = 0x52; sector[1] = 0x52; sector[2] = 0x61; sector[3] = 0x41;  /* RRaA */
    sector[484] = 0x72; sector[485] = 0x72; sector[486] = 0x41; sector[487] = 0x61;  /* rrAa */
    /* Free clusters = unknown */
    sector[488] = 0xFF; sector[489] = 0xFF; sector[490] = 0xFF; sector[491] = 0xFF;
    /* Next free = 3 */
    sector[492] = 3; sector[493] = 0; sector[494] = 0; sector[495] = 0;
    sector[510] = 0x55; sector[511] = 0xAA;
    
    ahci_write_sector(disk->port, part1_start + 1, sector);
    
    /* Initialize FAT table */
    for (int i = 0; i < 512; i++) sector[i] = 0;
    sector[0] = 0xF8; sector[1] = 0xFF; sector[2] = 0xFF; sector[3] = 0x0F;  /* Media + reserved */
    sector[4] = 0xFF; sector[5] = 0xFF; sector[6] = 0xFF; sector[7] = 0x0F;  /* Cluster 1 reserved */
    sector[8] = 0xFF; sector[9] = 0xFF; sector[10] = 0xFF; sector[11] = 0x0F; /* Root dir end */
    
    ahci_write_sector(disk->port, part1_start + 32, sector);
    ahci_write_sector(disk->port, part1_start + 32 + fat_size, sector);  /* Second FAT copy */
    
    serial_puts("[INSTALL] EFI partition formatted\n");
    
    /* ===== Format Recovery and System partitions as NXFS ===== */
    serial_puts("[INSTALL] Formatting NXFS partitions...\n");
    
    /* Set up AHCI block operations for NXFS (using file-level functions) */
    extern void nxfs_set_block_ops(void *ops);
    extern int nxfs_format(void *device, uint64_t total_blocks, const char *volume_name, int encrypted, const uint8_t *key);
    
    nxfs_set_block_ops(&ahci_block_ops);
    
    /* Format Recovery partition */
    nxfs_part_start_lba = part2_start;
    nxfs_ahci_port = disk->port;
    nxfs_format((void *)(uintptr_t)disk->port, recovery_size / 8, "Recovery", 0, NULL);
    
    /* Format System partition */
    uint64_t system_size = part3_end - part3_start;
    nxfs_part_start_lba = part3_start;
    nxfs_format((void *)(uintptr_t)disk->port, system_size / 8, "NeolyxOS", 0, NULL);
    
    serial_puts("[INSTALL] All partitions formatted\n");
    return 0;
}

int installer_copy_files(disk_info_t *disk, profile_type_t profile) {
    serial_puts("[INSTALL] Copying system files...\n");
    
    /* Get kernel binary from RAM (loaded by bootloader) */
    uint64_t kernel_physical_base = g_boot_info->kernel_physical_base;
    uint64_t kernel_size = g_boot_info->kernel_size;
    
    /* Create EFI/BOOT directory structure */
    serial_puts("[INSTALL] Creating EFI directory structure...\n");
    vfs_create("/EFI", VFS_DIRECTORY, 0755);
    vfs_create("/EFI/BOOT", VFS_DIRECTORY, 0755);
    
    /* Copy kernel.bin */
    serial_puts("[INSTALL] Copying kernel.bin...\n");
    vfs_file_t *kernel_file = vfs_open("/EFI/BOOT/kernel.bin", VFS_O_WRONLY | VFS_O_CREATE);
    if (kernel_file) {
        /* Write kernel from memory */
        if (kernel_physical_base && kernel_size > 0) {
            vfs_write(kernel_file, (void *)kernel_physical_base, kernel_size);
        }
        vfs_close(kernel_file);
    }
    
    /* Create installation marker */
    serial_puts("[INSTALL] Creating installation marker...\n");
    vfs_file_t *marker = vfs_open("/EFI/BOOT/instald.mrk", VFS_O_WRONLY | VFS_O_CREATE);
    if (marker) {
        const char *content = "NeolyxOS Installed\n";
        vfs_write(marker, content, 19);
        
        const char *prof = (profile == PROFILE_DESKTOP) ? "Edition: Desktop\n" : "Edition: Server\n";
        vfs_write(marker, prof, 17);
        
        vfs_close(marker);
    }
    
    /* Create system directories */
    /* NOTE: Skip directory structure creation during install because:
     *   - The ESP (FAT32) is currently mounted at /
     *   - The NXFS system partition isn't mounted yet
     *   - Directory structure will be created on first boot when NXFS is mounted
     */
    serial_puts("[INSTALL] Directory structure deferred to first boot\n");
    
    /* Profile-specific setup - also deferred to first boot */
    serial_puts("[INSTALL] Profile: ");
    serial_puts((profile == PROFILE_DESKTOP) ? "Desktop\n" : "Server\n");
    
    serial_puts("[INSTALL] File copy complete\n");
    (void)disk;
    return 0;
}
