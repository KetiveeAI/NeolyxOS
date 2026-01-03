/*
 * NeolyxOS Boot Manager
 * 
 * ARCHITECTURE NOTE:
 * This is the BOOT MANAGER that provides the graphical boot menu.
 * The boot sequence is:
 *   1. UEFI Bootloader loads kernel.bin
 *   2. startup.S calls kernel_main() defined HERE
 *   3. Boot Manager shows menu (Install, Boot, Restore, etc.)
 *   4. When "Boot NeolyxOS" selected, calls nx_kernel_start() from main.c
 *
 * DO NOT rename kernel_main() - it is called by startup.S!
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "include/ui/disk_icons.h"
#include "include/ui/cursors.h"
#include "include/ui/nxfont.h"
#include "include/ui/vector_icons_v2.h"

/* Forward declarations */
static void draw_nucleus_disk(int selection);
static void draw_boot_options(int selection);
static void draw_docs(int page);
static void draw_about_page(void);


/* Boot info structure (must match bootloader) */
typedef struct {
    uint32_t magic;
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

#define NEOLYX_BOOT_MAGIC 0x4E454F58

/* PS/2 Keyboard ports */
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

/* Serial port */
#define COM1_PORT 0x3F8

/* I/O functions */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Serial functions */
static void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

static void serial_putc(char c) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0);
    outb(COM1_PORT, c);
}

static void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

/* Framebuffer state */
static volatile uint32_t *framebuffer = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;

/* Colors */
#define COLOR_BG        0x00101020
#define COLOR_ACCENT    0x0000B4D8
#define COLOR_WHITE     0x00FFFFFF
#define COLOR_GRAY      0x00808090
#define COLOR_SELECTED  0x00304060
#define COLOR_BOX_BG    0x00202040
#define COLOR_BORDER    0x00404080
#define COLOR_GREEN     0x0000D4A8
#define COLOR_YELLOW    0x00F0D040
#define COLOR_RED       0x00D04040

/* Draw pixel */
static void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (framebuffer && x < fb_width && y < fb_height) {
        framebuffer[y * (fb_pitch / 4) + x] = color;
    }
}

/* Fill rectangle */
static void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h && (y + j) < fb_height; j++) {
        for (uint32_t i = 0; i < w && (x + i) < fb_width; i++) {
            fb_put_pixel(x + i, y + j, color);
        }
    }
}

/* Clear screen */
static void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, fb_width, fb_height, color);
}

/* Simple 8x16 font for text */
static const uint8_t font_basic[95][16] = {
    /* Space ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* ! */ {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* " - ' */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* ( */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 0-9 */
    {0x00,0x00,0x7C,0xC6,0xC6,0xCE,0xDE,0xF6,0xE6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 0 */
    {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00}, /* 1 */
    {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00}, /* 2 */
    {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 3 */
    {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00}, /* 4 */
    {0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 5 */
    {0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 6 */
    {0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00}, /* 7 */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* 8 */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00}, /* 9 */
    /* : - @ */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* A-Z */
    {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* A */
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00}, /* B */
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00}, /* C */
    {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00}, /* D */
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00}, /* E */
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, /* F */
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00}, /* G */
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* H */
    {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, /* I */
    {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00}, /* J */
    {0x00,0x00,0xE6,0x66,0x6C,0x6C,0x78,0x6C,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, /* K */
    {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00}, /* L */
    {0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* M */
    {0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* N */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* O */
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, /* P */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x00,0x00,0x00,0x00}, /* Q */
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, /* R */
    {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* S */
    {0x00,0x00,0x7E,0x7E,0x5A,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, /* T */
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* U */
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00}, /* V */
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00}, /* W */
    {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00}, /* X */
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, /* Y */
    {0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00}, /* Z */
    /* [ - ` */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* a-z */
    {0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, /* a */
    {0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00}, /* b */
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* c */
    {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, /* d */
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* e */
    {0x00,0x00,0x38,0x6C,0x64,0x60,0xF0,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, /* f */
    {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00}, /* g */
    {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, /* h */
    {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, /* i */
    {0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00}, /* j */
    {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00}, /* k */
    {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, /* l */
    {0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00}, /* m */
    {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00}, /* n */
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* o */
    {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, /* p */
    {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00}, /* q */
    {0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, /* r */
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00}, /* s */
    {0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00}, /* t */
    {0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, /* u */
    {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00}, /* v */
    {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00}, /* w */
    {0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, /* x */
    {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00}, /* y */
    {0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00}, /* z */
};

/* Draw character */
static void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t color) {
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = font_basic[c - 32];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                fb_put_pixel(x + col, y + row, color);
            }
        }
    }
}

/* Draw string */
static void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t color) {
    while (*s) {
        fb_draw_char(x, y, *s, color);
        x += 9;
        s++;
    }
}

/* Draw centered string */
static void fb_draw_centered(uint32_t y, const char *s, uint32_t color) {
    int len = 0;
    const char *p = s;
    while (*p++) len++;
    uint32_t x = (fb_width - len * 9) / 2;
    fb_draw_string(x, y, s, color);
}

/* Draw NeolyxOS logo */


/* ============ Boot Options / Settings ============ */

/* Boot flags - can be toggled in Settings menu */
static struct {
    uint8_t safe_mode;        /* Boot with minimal drivers */
    uint8_t verbose_boot;     /* Show detailed boot messages */
    uint8_t single_user;      /* Boot to root shell */
    uint8_t network_off;      /* Disable networking */
    uint8_t debug_mode;       /* Enable kernel debugging */
} g_boot_options = {0, 0, 0, 0, 0};

/* Platform detection */
static int g_is_vm = 0;       /* 1 if running in VM */
static int g_is_physical = 0; /* 1 if real hardware */

static void detect_platform(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x40000000));
    
    g_is_vm = (ebx == 0x4B4D564B); /* "KVMK" = KVM/QEMU */
    if (!g_is_vm) {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        g_is_vm = (ecx & (1 << 31)) != 0;  /* Hypervisor present bit */
    }
    g_is_physical = !g_is_vm;
    
    serial_puts("[PLATFORM] ");
    serial_puts(g_is_vm ? "Virtual Machine detected\n" : "Physical hardware detected\n");
}

/* Main Menu options (left panel) */
#define MENU_MAIN_COUNT 5
static const char *menu_main_labels[MENU_MAIN_COUNT] = {
    "Boot NeolyxOS",

    "Boot Options",
    "Install NeolyxOS",
    "Restore System",
    "Nucleus Disk"

};



/* Sidebar options (right panel) */
#define MENU_SIDEBAR_COUNT 7
static const char *menu_sidebar_labels[MENU_SIDEBAR_COUNT] = {
    "Exit NeolyxOS",
    "UEFI Settings",
    "Terminal",
    "Support/Docs",
    "Recovery Shell",
    "Zepra Browser",
    "About"
};



/* View State for Navigation */
typedef enum {
    VIEW_MAIN,
    VIEW_BOOT_OPTIONS,
    VIEW_DOCS,
    VIEW_ABOUT,
    VIEW_NUCLEUS_DISK,
    VIEW_OS_SELECT,        /* Server/Desktop selection */
    VIEW_DISK_SELECT,      /* Disk picker for install */
    VIEW_INSTALL_PROGRESS  /* Install progress bar */
} view_t;

static view_t current_view = VIEW_MAIN;
static int sub_selection = 0; /* Selection within sub-pages */

/* Colors for new design - brighter palette for better visibility */

/* Colors for new design - brighter palette for better visibility */
#define COLOR_PURPLE_BG    0x001A1A2E  /* Deep navy background */
#define COLOR_PURPLE_BOX   0x0030305A  /* Box background - more contrast */
#define COLOR_PURPLE_LIGHT 0x00505080  /* Highlighted button background */
#define COLOR_ICON_MAIN    0x0060E0FF  /* Bright cyan for main icons */
#define COLOR_ICON_SIDE    0x00AADDFF  /* Light blue for sidebar icons */
#define COLOR_HIGHLIGHT    0x0000D4FF  /* Bright cyan selection highlight */
#define COLOR_BORDER_GLOW  0x0080C0FF  /* Selection border glow */

/* Draw rounded box (approximated with rectangles) */
static void draw_box_rounded(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    fb_fill_rect(x + 4, y, w - 8, h, color);
    fb_fill_rect(x, y + 4, 4, h - 8, color);
    fb_fill_rect(x + w - 4, y + 4, 4, h - 8, color);
}

/* Draw glow border effect */


/* ============ PS/2 Mouse Driver ============ */

#define MOUSE_DATA_PORT    0x60
#define MOUSE_STATUS_PORT  0x64
#define MOUSE_CMD_PORT     0x64

static int32_t mouse_x = 400;
static int32_t mouse_y = 300;
static uint8_t mouse_buttons = 0;
static int mouse_initialized = 0;

/* Cursor bitmap (12x16 arrow) */
static const uint16_t cursor_bitmap[16] = {
    0x8000, 0xC000, 0xE000, 0xF000,
    0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xF800, 0xD800, 0x8C00, 0x0C00,
    0x0600, 0x0600, 0x0300, 0x0000
};

/* Saved background under cursor */
static uint32_t cursor_bg[16][12];
static int cursor_drawn = 0;

static void mouse_wait_write(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(MOUSE_STATUS_PORT) & 0x02)) return;
    }
}

static void mouse_wait_read(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(MOUSE_STATUS_PORT) & 0x01) return;
    }
}

static void mouse_cmd(uint8_t cmd) {
    mouse_wait_write();
    outb(MOUSE_CMD_PORT, 0xD4);
    mouse_wait_write();
    outb(MOUSE_DATA_PORT, cmd);
    mouse_wait_read();
    inb(MOUSE_DATA_PORT); /* ACK */
}

static void mouse_init(void) {
    /* Enable auxiliary device */
    mouse_wait_write();
    outb(MOUSE_CMD_PORT, 0xA8);
    
    /* Enable interrupts */
    mouse_wait_write();
    outb(MOUSE_CMD_PORT, 0x20);
    mouse_wait_read();
    uint8_t status = inb(MOUSE_DATA_PORT) | 0x02;
    mouse_wait_write();
    outb(MOUSE_CMD_PORT, 0x60);
    mouse_wait_write();
    outb(MOUSE_DATA_PORT, status);
    
    /* Use default settings */
    mouse_cmd(0xF6);
    
    /* Enable mouse */
    mouse_cmd(0xF4);
    
    mouse_initialized = 1;
    serial_puts("[OK] Mouse initialized\n");
}

static void cursor_save_bg(void) {
    if (!framebuffer) return;
    for (int y = 0; y < 16 && (mouse_y + y) < (int)fb_height; y++) {
        for (int x = 0; x < 12 && (mouse_x + x) < (int)fb_width; x++) {
            if (mouse_x + x >= 0 && mouse_y + y >= 0) {
                cursor_bg[y][x] = framebuffer[(mouse_y + y) * (fb_pitch / 4) + (mouse_x + x)];
            }
        }
    }
}

static void cursor_restore_bg(void) {
    if (!framebuffer || !cursor_drawn) return;
    for (int y = 0; y < 16 && (mouse_y + y) < (int)fb_height; y++) {
        for (int x = 0; x < 12 && (mouse_x + x) < (int)fb_width; x++) {
            if (mouse_x + x >= 0 && mouse_y + y >= 0) {
                framebuffer[(mouse_y + y) * (fb_pitch / 4) + (mouse_x + x)] = cursor_bg[y][x];
            }
        }
    }
    cursor_drawn = 0;
}

static void cursor_draw(void) {
    if (!framebuffer) return;
    cursor_save_bg();
    for (int y = 0; y < 16 && (mouse_y + y) < (int)fb_height; y++) {
        uint16_t row = cursor_bitmap[y];
        for (int x = 0; x < 12 && (mouse_x + x) < (int)fb_width; x++) {
            if ((row & (0x8000 >> x)) && mouse_x + x >= 0 && mouse_y + y >= 0) {
                framebuffer[(mouse_y + y) * (fb_pitch / 4) + (mouse_x + x)] = COLOR_WHITE;
            }
        }
    }
    cursor_drawn = 1;
}

static int mouse_poll(int8_t *dx, int8_t *dy, uint8_t *buttons) {
    /* Check if mouse data available (bit 0 = data ready, bit 5 = from mouse) */
    uint8_t status = inb(MOUSE_STATUS_PORT);
    if (!(status & 0x01)) return 0;  /* No data */
    
    /* Read first byte */
    uint8_t byte1 = inb(MOUSE_DATA_PORT);
    
    /* Bit 3 must be set in first byte of mouse packet */
    if (!(byte1 & 0x08)) {
        /* Out of sync - discard and try to resync */
        return 0;
    }
    
    /* Wait for second byte with timeout */
    for (int i = 0; i < 10000; i++) {
        if (inb(MOUSE_STATUS_PORT) & 0x01) break;
    }
    if (!(inb(MOUSE_STATUS_PORT) & 0x01)) return 0;
    uint8_t byte2 = inb(MOUSE_DATA_PORT);
    
    /* Wait for third byte with timeout */
    for (int i = 0; i < 10000; i++) {
        if (inb(MOUSE_STATUS_PORT) & 0x01) break;
    }
    if (!(inb(MOUSE_STATUS_PORT) & 0x01)) return 0;
    uint8_t byte3 = inb(MOUSE_DATA_PORT);
    
    /* Parse packet - handle sign extension */
    int32_t raw_dx = byte2;
    int32_t raw_dy = byte3;
    
    if (byte1 & 0x10) raw_dx |= 0xFFFFFF00;  /* Sign extend X */
    if (byte1 & 0x20) raw_dy |= 0xFFFFFF00;  /* Sign extend Y */
    
    /* Clamp to int8 range */
    if (raw_dx > 127) raw_dx = 127;
    if (raw_dx < -128) raw_dx = -128;
    if (raw_dy > 127) raw_dy = 127;
    if (raw_dy < -128) raw_dy = -128;
    
    *dx = (int8_t)raw_dx;
    *dy = (int8_t)(-raw_dy);  /* Invert Y for screen */
    *buttons = byte1 & 0x07;
    return 1;
}

/* ============ Boot Manager UI ============ */

/* Vector icon pointers for menu items */
static const vic_path_t *menu_main_icons_v2[MENU_MAIN_COUNT] = {
    icon_neolyx_v2,
    icon_settings_v2,
    icon_install_v2,
    icon_restore_v2,
    icon_disk_v2
};

static const vic_path_t *menu_sidebar_icons_v2[MENU_SIDEBAR_COUNT] = {
    icon_exit_v2,
    icon_settings_v2,
    icon_terminal_v2,
    icon_help_v2,
    icon_warning_v2,
    icon_browser_v2,
    icon_neolyx_v2
};

/* Draw the new Boot Manager UI - FULLSCREEN with vector rendering */
static void draw_boot_manager(int selected, int in_sidebar) {
    /* Clear with dark background - fills entire screen */
    fb_clear(COLOR_PURPLE_BG);
    
    /* Calculate adaptive sizing based on screen resolution */
    int title_size = fb_height > 600 ? 32 : 24;
    int menu_size = fb_height > 600 ? 20 : 16;
    int icon_size = fb_height > 600 ? 48 : 36;
    int small_icon = fb_height > 600 ? 32 : 24;
    int padding = fb_height > 600 ? 20 : 12;
    
    /* ===== Title Bar ===== */
    vic_rounded_rect_fill(framebuffer, fb_pitch, 
        padding, padding, fb_width - padding * 2, 60,
        12, COLOR_PURPLE_BOX, fb_width, fb_height);
    
    /* Title text */
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, padding + 15, 
                      "NeolyxOS Boot Manager", title_size, COLOR_WHITE, 
                      fb_width, fb_height);
    
    /* Version in top right */
    nxf_draw_text(framebuffer, fb_pitch, fb_width - 180, padding + 22,
                  "v1.0.0 Alpha", 14, 0x00808090, fb_width, fb_height);
    
    /* ===== Main Menu Panel (Left) ===== */
    uint32_t main_x = padding * 2;
    uint32_t main_y = 100;
    uint32_t main_w = fb_width / 2 - padding * 3;
    uint32_t item_h = icon_size + padding * 2;
    uint32_t main_h = MENU_MAIN_COUNT * item_h + padding * 2;
    
    /* Panel background */
    vic_rounded_rect_fill(framebuffer, fb_pitch,
        main_x, main_y, main_w, main_h, 16,
        COLOR_PURPLE_BOX, fb_width, fb_height);
    
    /* Panel border */
    vic_rounded_rect(framebuffer, fb_pitch,
        main_x, main_y, main_w, main_h, 16,
        2.0f, COLOR_BORDER, fb_width, fb_height);
    
    /* Menu items */
    for (int i = 0; i < MENU_MAIN_COUNT; i++) {
        uint32_t item_y = main_y + padding + i * item_h;
        uint32_t item_x = main_x + padding;
        
        /* Selection highlight */
        if (!in_sidebar && i == selected) {
            vic_rounded_rect_fill(framebuffer, fb_pitch,
                item_x, item_y, main_w - padding * 2, item_h - 8, 10,
                COLOR_PURPLE_LIGHT, fb_width, fb_height);
            vic_rounded_rect(framebuffer, fb_pitch,
                item_x, item_y, main_w - padding * 2, item_h - 8, 10,
                2.5f, COLOR_ACCENT, fb_width, fb_height);
        }
        
        /* Vector icon */
        uint32_t icon_color = (!in_sidebar && i == selected) ? COLOR_HIGHLIGHT : COLOR_ICON_MAIN;
        if (menu_main_icons_v2[i]) {
            if (!in_sidebar && i == selected) {
                vic_render_icon_glow(framebuffer, fb_pitch,
                    item_x + 8, item_y + (item_h - icon_size) / 2 - 4,
                    icon_size, menu_main_icons_v2[i], icon_color, fb_width, fb_height);
            } else {
                vic_render_icon(framebuffer, fb_pitch,
                    item_x + 8, item_y + (item_h - icon_size) / 2 - 4,
                    icon_size, menu_main_icons_v2[i], icon_color, fb_width, fb_height);
            }
        }
        
        /* Text label */
        uint32_t text_color = (!in_sidebar && i == selected) ? COLOR_WHITE : 0x00C0C0D0;
        nxf_draw_text(framebuffer, fb_pitch,
            item_x + icon_size + 20, item_y + (item_h - menu_size) / 2 - 4,
            menu_main_labels[i], menu_size, text_color, fb_width, fb_height);
    }
    
    /* ===== Sidebar Panel (Right) ===== */
    uint32_t side_x = fb_width / 2 + padding;
    uint32_t side_y = 100;
    uint32_t side_w = fb_width / 2 - padding * 3;
    uint32_t side_item_h = small_icon + padding;
    uint32_t side_h = MENU_SIDEBAR_COUNT * side_item_h + padding * 2;
    
    /* Panel background */
    vic_rounded_rect_fill(framebuffer, fb_pitch,
        side_x, side_y, side_w, side_h, 16,
        COLOR_PURPLE_BOX, fb_width, fb_height);
    
    /* Panel border */
    vic_rounded_rect(framebuffer, fb_pitch,
        side_x, side_y, side_w, side_h, 16,
        2.0f, COLOR_BORDER, fb_width, fb_height);
    
    /* Sidebar items */
    for (int i = 0; i < MENU_SIDEBAR_COUNT; i++) {
        uint32_t item_y = side_y + padding + i * side_item_h;
        uint32_t item_x = side_x + padding;
        
        /* Selection highlight */
        if (in_sidebar && i == selected) {
            vic_rounded_rect_fill(framebuffer, fb_pitch,
                item_x, item_y, side_w - padding * 2, side_item_h - 6, 8,
                COLOR_PURPLE_LIGHT, fb_width, fb_height);
            vic_rounded_rect(framebuffer, fb_pitch,
                item_x, item_y, side_w - padding * 2, side_item_h - 6, 8,
                2.0f, COLOR_ACCENT, fb_width, fb_height);
        }
        
        /* Vector icon */
        uint32_t icon_color = (in_sidebar && i == selected) ? COLOR_HIGHLIGHT : COLOR_ICON_SIDE;
        if (menu_sidebar_icons_v2[i]) {
            vic_render_icon(framebuffer, fb_pitch,
                item_x + 4, item_y + (side_item_h - small_icon) / 2 - 3,
                small_icon, menu_sidebar_icons_v2[i], icon_color, fb_width, fb_height);
        }
        
        /* Text label */
        uint32_t text_color = (in_sidebar && i == selected) ? COLOR_WHITE : 0x00A0A0B0;
        int label_size = menu_size > 18 ? 16 : 14;
        nxf_draw_text(framebuffer, fb_pitch,
            item_x + small_icon + 12, item_y + (side_item_h - label_size) / 2 - 3,
            menu_sidebar_labels[i], label_size, text_color, fb_width, fb_height);
    }
    
    /* ===== Footer with keyboard hints ===== */
    int footer_h = 40;
    uint32_t footer_y = fb_height - footer_h - 15;
    
    /* Footer background */
    vic_rounded_rect_fill(framebuffer, fb_pitch,
        padding, footer_y, fb_width - padding * 2, footer_h, 8,
        COLOR_PURPLE_BOX, fb_width, fb_height);
    
    /* Adaptive hint sizing */
    int hint_size = (fb_width < 1024) ? 10 : ((fb_width < 1600) ? 12 : 14);
    int grp_spacing = fb_width / 5;  /* Scale with screen */
    int center = fb_width / 2;
    int text_y = footer_y + (footer_h - hint_size) / 2 - 2;
    
    /* Draw as single centered line for simplicity */
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, text_y,
        "Enter Select   Arrows Navigate   Esc Back", hint_size, 0x00909090, fb_width, fb_height);
    
    /* Mouse hint below footer if there's room */
    if (mouse_initialized && fb_height > 500) {
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, footer_y + footer_h + 5,
            "Mouse: Click to select", 10, 0x00505060, fb_width, fb_height);
    }
}



/* Check for key (non-blocking) */
static int key_available(void) {
    return (inb(KEYBOARD_STATUS_PORT) & 0x01) != 0;
}

/* Get key (non-blocking, returns 0 if none) */
static uint8_t get_key(void) {
    if (key_available()) {
        return inb(KEYBOARD_DATA_PORT);
    }
    return 0;
}

/* Wait for key (blocking with polling) */
static uint8_t wait_key(void) {
    /* Use polling instead of HLT (HLT needs interrupts enabled) */
    while (!key_available()) {
        /* Small delay to reduce CPU usage */
        for (volatile int i = 0; i < 1000; i++);
    }
    return inb(KEYBOARD_DATA_PORT);
}





/* Execute command */

/* ============ Full Keyboard Driver ============ */

/* Keyboard state */


/* Process scancode and update keyboard state */



/* ============ NUCLEUS Disk Utility ============ */

/* Disk info structure for NUCLEUS */
typedef struct {
    int index;
    char name[8];
    char model[48];
    uint64_t size_bytes;
    int partition_count;
    int is_boot_disk;
} nucleus_disk_t;

/* Partition info structure */
typedef struct {
    int index;
    int disk_index;  /* Which disk this partition belongs to */
    char label[32];
    char fs_type[16];
    uint64_t size_bytes;
    uint64_t used_bytes;
    int is_system;
} nucleus_partition_t;

/* String helpers for Nucleus (nx_strlen removed as unused) */
static void nx_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* NUCLEUS state */
static nucleus_disk_t nucleus_disks[8];
static int nucleus_disk_count = 0;
static nucleus_partition_t nucleus_partitions[16];
static int nucleus_partition_count = 0;

/* Nucleus UI State */
static int nucleus_modal_active = 0; /* 0=None, 1=Actions Menu, 2=Format Confirm */
static int nucleus_modal_selection = 0;


/* Scan for disks using AHCI driver */
static void nucleus_scan_disks(void) {
    serial_puts("[NUCLEUS] Scanning disks...\n");
    nucleus_disk_count = 0;
    
    /* Try AHCI detection (requires AHCI driver) */
    /* For now, detect the boot disk from UEFI that we're running from */
    /* In QEMU, the disk image we booted from is the only real disk */
    
    /* Check if we're in a VM by looking for QEMU signature in CPUID */
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x40000000));
    
    int is_vm = (ebx == 0x4B4D564B); /* "KVMK" = KVM/QEMU */
    if (!is_vm) {
        /* Check for other hypervisors */
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        is_vm = (ecx & (1 << 31)) != 0;  /* Hypervisor present bit */
    }
    
    if (is_vm) {
        serial_puts("[NUCLEUS] Running in virtual machine\n");
        /* In VM: show the boot disk we came from */
        nucleus_disks[0].index = 0;
        nx_strcpy(nucleus_disks[0].name, "vda", 8);
        nx_strcpy(nucleus_disks[0].model, "Boot Disk (VM)", 48);
        nucleus_disks[0].size_bytes = 64 * 1024 * 1024;  /* 64MB - the actual image */
        nucleus_disks[0].partition_count = 1;
        nucleus_disks[0].is_boot_disk = 1;
        nucleus_disk_count = 1;
    } else {
        serial_puts("[NUCLEUS] Running on physical hardware\n");
        /* On real hardware: would probe AHCI/NVMe controllers */
        /* For now, no disks detected until drivers are integrated */
        nucleus_disk_count = 0;
    }
    
    serial_puts("[NUCLEUS] Found ");
    serial_putc('0' + nucleus_disk_count);
    serial_puts(" disk(s)\n");
}

/* Load partitions for selected disk */
static void nucleus_load_partitions(int disk_idx) {
    nucleus_partition_count = 0;
    
    if (nucleus_disk_count == 0) {
        return;  /* No disks, no partitions */
    }
    
    if (disk_idx == 0) {
        /* Boot disk - EFI partition only (the one we booted from) */
        nucleus_partitions[0].index = 0;
        nucleus_partitions[0].disk_index = 0;
        nx_strcpy(nucleus_partitions[0].label, "EFI System", 32);
        nx_strcpy(nucleus_partitions[0].fs_type, "FAT32", 16);
        nucleus_partitions[0].size_bytes = 64 * 1024 * 1024;  /* 64MB - actual image */
        nucleus_partitions[0].used_bytes = 2 * 1024 * 1024;   /* ~2MB used */
        nucleus_partitions[0].is_system = 1;
        nucleus_partition_count = 1;
    } else if (disk_idx == 1 && nucleus_disk_count > 1) {
        /* USB disk - simulate with NXFS partition for testing install */
        nucleus_partitions[0].index = 0;
        nucleus_partitions[0].disk_index = 1;
        nx_strcpy(nucleus_partitions[0].label, "NeolyxOS Install", 32);
        nx_strcpy(nucleus_partitions[0].fs_type, "NXFS", 16);
        nucleus_partitions[0].size_bytes = 16ULL * 1024 * 1024 * 1024;  /* 16GB */
        nucleus_partitions[0].used_bytes = 0;
        nucleus_partitions[0].is_system = 0;
        nucleus_partition_count = 1;
    }
}

/* Format size to string (GB/MB) */
static void nucleus_format_size(uint64_t bytes, char *out, int max) {
    uint64_t gb = bytes / (1024 * 1024 * 1024);
    uint64_t mb = (bytes / (1024 * 1024)) % 1024;
    
    int i = 0;
    if (gb > 0) {
        if (gb >= 100) out[i++] = '0' + (gb / 100) % 10;
        if (gb >= 10) out[i++] = '0' + (gb / 10) % 10;
        out[i++] = '0' + gb % 10;
        out[i++] = ' ';
        out[i++] = 'G';
        out[i++] = 'B';
    } else {
        if (mb >= 100) out[i++] = '0' + (mb / 100) % 10;
        if (mb >= 10) out[i++] = '0' + (mb / 10) % 10;
        out[i++] = '0' + mb % 10;
        out[i++] = ' ';
        out[i++] = 'M';
        out[i++] = 'B';
    }
    out[i] = '\0';
    (void)max;
}

/* Draw NUCLEUS UI */


/* NUCLEUS main function - with mouse support */
/* Legacy functions show_nucleus_utility and show_terminal removed */

/* ============ Kernel Entry ============ */

/*
 * Forward declaration for the full kernel entry point.
 * nx_kernel_start() is defined in src/core/main.c
 * It initializes all kernel subsystems and runs the OS.
 */
extern void nx_kernel_start(void);

/* Global boot info pointer for passing to kernel */
static NeolyxBootInfo *g_boot_info = 0;

/*
 * kernel_main - Boot Manager Entry Point
 *
 * Called by startup.S after CPU initialization.
 * Shows the graphical boot menu and handles user selection.
 */
/* ========================================================================= */
/*                          PAGE IMPLEMENTATIONS                             */
/* ========================================================================= */

#ifndef COLOR_GREEN
#define COLOR_GREEN 0xFF00FF00
#endif

static uint8_t* get_boot_opt_ptr(int index) {
    switch(index) {
        case 0: return &g_boot_options.safe_mode;
        case 1: return &g_boot_options.verbose_boot;
        case 2: return &g_boot_options.single_user;
        case 3: return &g_boot_options.network_off;
        case 4: return &g_boot_options.debug_mode;
        default: return 0;
    }
}


static void draw_boot_options(int selection) {
    /* Clear background */
    fb_clear(COLOR_PURPLE_BG);
    
    /* Header */
    fb_fill_rect(0, 0, fb_width, 80, COLOR_PURPLE_BOX);
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 25, "Boot Configuration", 48, COLOR_WHITE, fb_width, fb_height);
    
    const char *opts[] = {
        "Safe Mode (No drivers)",
        "Verbose Boot Log",
        "Single User Mode",
        "Disable Network Stack",
        "Kernel Debug Mode"
    };
    
    uint32_t start_y = 150;
    
    for (int i = 0; i < 5; i++) {
        uint32_t y = start_y + i * 60;
        uint32_t x = fb_width / 2 - 300;
        
        // uint32_t bg_color = (i == selection) ? COLOR_ACCENT : COLOR_PURPLE_BOX; /* Unused now */
        
        /* Draw selection box */
        if (i == selection) {
             /* Rect fill: fb, pitch, x, y, w, h, radius, color, max_x, max_y */
             vic_rounded_rect_fill(framebuffer, fb_pitch, x - 10, y - 10, 600, 50, 10, COLOR_ACCENT, fb_width, fb_height); 
             /* Rect outline: fb, pitch, x, y, w, h, radius, thickness, color, max_x, max_y */
             vic_rounded_rect(framebuffer, fb_pitch, x - 10, y - 10, 600, 50, 10, 2, COLOR_WHITE, fb_width, fb_height);
        }
        
        /* Draw Checkbox */
        uint8_t *val_ptr = get_boot_opt_ptr(i);
        int checked = val_ptr ? *val_ptr : 0;
        
        vic_rounded_rect_fill(framebuffer, fb_pitch, x, y, 30, 30, 5, checked ? COLOR_GREEN : 0xFF202020, fb_width, fb_height);
        vic_rounded_rect(framebuffer, fb_pitch, x, y, 30, 30, 5, 2, COLOR_WHITE, fb_width, fb_height);
        
        if (checked) {
             /* Simple checkmark */
             vic_rounded_rect_fill(framebuffer, fb_pitch, x+8, y+8, 14, 14, 2, COLOR_WHITE, fb_width, fb_height);
        }
        
        nxf_draw_text(framebuffer, fb_pitch, x + 50, y, opts[i], 32, COLOR_WHITE, fb_width, fb_height);
    }
    
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 60, "[Enter] Toggle   [Esc] Back", 24, 0x00A0A0B0, fb_width, fb_height);
}


static void draw_nucleus_disk(int selection) {
    /* Clear */
    fb_clear(COLOR_PURPLE_BG);
    
    /* Adaptive font sizing based on resolution */
    /* Base: 1024x768.  Scale up for larger screens */
    int scale = 1;
    if (fb_width >= 1920) scale = 2;
    else if (fb_width >= 1280) scale = 1;
    
    int font_title = 24 + scale * 8;      /* 32-40 */
    int font_large = 16 + scale * 4;      /* 20-24 */
    int font_med = 12 + scale * 3;        /* 15-18 */
    int font_small = 10 + scale * 2;      /* 12-14 */
    
    int label_gap = 80 + scale * 20;      /* Gap between label and value */
    
    /* Header */
    fb_fill_rect(0, 0, fb_width, 60 + scale * 10, COLOR_PURPLE_BOX);
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 15 + scale * 5, "Nucleus Disk Manager", font_title, COLOR_WHITE, fb_width, fb_height);
    
    /* Left Panel: Disk List */
    int panel_x = 30 + scale * 10;
    int panel_y = 80 + scale * 20;
    int panel_w = 180 + scale * 60;
    int panel_h = fb_height - panel_y - 80;
    
    vic_rounded_rect_fill(framebuffer, fb_pitch, panel_x, panel_y, panel_w, panel_h, 8, COLOR_PURPLE_BOX, fb_width, fb_height);
    
    if (nucleus_disk_count == 0) {
        nxf_draw_text(framebuffer, fb_pitch, panel_x + 10, panel_y + 20, "No disks", font_med, COLOR_GRAY, fb_width, fb_height);
    } else {
        for (int i = 0; i < nucleus_disk_count && i < 8; i++) {
            int dy = panel_y + 15 + i * (40 + scale * 10);
            
            if (i == selection) {
                vic_rounded_rect_fill(framebuffer, fb_pitch, panel_x + 5, dy - 5, panel_w - 10, 35 + scale * 5, 5, 0xFF404060, fb_width, fb_height);
            }
            
            /* Icon */
            int icon_sz = 20 + scale * 6;
            vic_render_icon(framebuffer, fb_pitch, panel_x + 10, dy, icon_sz, icon_disk_v2, i == selection ? COLOR_ACCENT : COLOR_GRAY, fb_width, fb_height);

            /* Name and size */
            nxf_draw_text(framebuffer, fb_pitch, panel_x + 15 + icon_sz, dy + 2, nucleus_disks[i].name, font_med, COLOR_WHITE, fb_width, fb_height);
            char size_s[16];
            nucleus_format_size(nucleus_disks[i].size_bytes, size_s, 16);
            nxf_draw_text(framebuffer, fb_pitch, panel_x + 15 + icon_sz, dy + 18, size_s, font_small, COLOR_GRAY, fb_width, fb_height);
        }
    }
    
    /* Right Panel: Details */
    int detail_x = panel_x + panel_w + 20;
    int detail_w = fb_width - detail_x - 30;
    
    vic_rounded_rect_fill(framebuffer, fb_pitch, detail_x, panel_y, detail_w, panel_h, 8, COLOR_PURPLE_BOX, fb_width, fb_height);
    
    if (nucleus_disk_count > 0 && selection < nucleus_disk_count) {
        nucleus_disk_t *d = &nucleus_disks[selection];
        
        int row_y = panel_y + 20;
        int row_h = 25 + scale * 5;
        
        /* Disk model/name */
        nxf_draw_text(framebuffer, fb_pitch, detail_x + 20, row_y, d->model, font_large, COLOR_WHITE, fb_width, fb_height);
        row_y += row_h + 10;
        
        char size_s[16];
        nucleus_format_size(d->size_bytes, size_s, 16);
        
        /* Size row */
        nxf_draw_text(framebuffer, fb_pitch, detail_x + 20, row_y, "Size:", font_med, COLOR_GRAY, fb_width, fb_height);
        nxf_draw_text(framebuffer, fb_pitch, detail_x + 20 + label_gap, row_y, size_s, font_med, COLOR_WHITE, fb_width, fb_height);
        row_y += row_h;
        
        /* Partitions count */
        char pcount[8] = "1";  /* Default */
        nxf_draw_text(framebuffer, fb_pitch, detail_x + 20, row_y, "Partitions:", font_med, COLOR_GRAY, fb_width, fb_height);
        nxf_draw_text(framebuffer, fb_pitch, detail_x + 20 + label_gap, row_y, pcount, font_med, COLOR_WHITE, fb_width, fb_height);
        row_y += row_h;
       
        /* Status */
        nxf_draw_text(framebuffer, fb_pitch, detail_x + 20, row_y, "Status:", font_med, COLOR_GRAY, fb_width, fb_height);
        nxf_draw_text(framebuffer, fb_pitch, detail_x + 20 + label_gap, row_y, d->is_boot_disk ? "Boot Disk" : "Online", font_med, d->is_boot_disk ? COLOR_GREEN : COLOR_WHITE, fb_width, fb_height);
        row_y += row_h + 15;

        /* Partitions List */
        nxf_draw_text(framebuffer, fb_pitch, detail_x + 20, row_y, "Partition Table:", font_med, COLOR_ACCENT, fb_width, fb_height);
        row_y += row_h;
        
        for (int p=0; p < nucleus_partition_count && p < 4; p++) {
             nucleus_partition_t *part = &nucleus_partitions[p];
             
             int part_h = 30 + scale * 5;
             vic_rounded_rect_fill(framebuffer, fb_pitch, detail_x + 20, row_y, detail_w - 40, part_h, 4, 0xFF303040, fb_width, fb_height);
             
             /* Icon */
             int pi_sz = 16 + scale * 4;
             vic_render_icon(framebuffer, fb_pitch, detail_x + 25, row_y + (part_h - pi_sz)/2, pi_sz, icon_disk_v2, part->is_system ? COLOR_ACCENT : COLOR_GRAY, fb_width, fb_height); 
             
             /* Label, FS type, Size - spread across the row */
             int col1 = detail_x + 30 + pi_sz;
             int col2 = col1 + 100 + scale * 30;
             int col3 = col2 + 60 + scale * 20;
             
             nxf_draw_text(framebuffer, fb_pitch, col1, row_y + (part_h - font_small)/2, part->label, font_small, COLOR_WHITE, fb_width, fb_height);
             nxf_draw_text(framebuffer, fb_pitch, col2, row_y + (part_h - font_small)/2, part->fs_type, font_small, COLOR_GRAY, fb_width, fb_height);
             
             nucleus_format_size(part->size_bytes, size_s, 16);
             nxf_draw_text(framebuffer, fb_pitch, col3, row_y + (part_h - font_small)/2, size_s, font_small, COLOR_WHITE, fb_width, fb_height);
             
             row_y += part_h + 5;
        }
    }
    
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 40, "[Up/Down] Select Disk   [Enter] Actions   [Esc] Back", font_med, 0x00A0A0B0, fb_width, fb_height);

    /* Modal Overlay */
    if (nucleus_modal_active) {
        /* Dim background */
        // fb_fill_rect(0, 0, fb_width, fb_height, 0x80000000); // Need alpha blend fill? No simple lib.
        // Just draw a box on top.
        
        uint32_t mx = fb_width/2 - 200;
        uint32_t my = fb_height/2 - 150;
        uint32_t mw = 400;
        uint32_t mh = 300;
        
        /* Shadow */
        vic_rounded_rect_fill(framebuffer, fb_pitch, mx+10, my+10, mw, mh, 15, 0xFF101010, fb_width, fb_height);
        /* Main Box */
        vic_rounded_rect_fill(framebuffer, fb_pitch, mx, my, mw, mh, 15, 0xFF2A2A35, fb_width, fb_height);
        vic_rounded_rect(framebuffer, fb_pitch, mx, my, mw, mh, 15, 2, COLOR_ACCENT, fb_width, fb_height);
        
        if (nucleus_modal_active == 1) {
            /* Actions Menu */
            nxf_draw_centered(framebuffer, fb_pitch, fb_width, my + 30, "Disk Actions", 32, COLOR_WHITE, fb_width, fb_height);
            
            const char *actions[] = {"Format Disk", "Create Partition", "Mount Disk", "Properties"};
            for (int i=0; i<4; i++) {
                uint32_t ay = my + 80 + i * 50;
                uint32_t color = (i == nucleus_modal_selection) ? COLOR_ACCENT : COLOR_GRAY;
                
                if (i == nucleus_modal_selection) {
                    vic_rounded_rect_fill(framebuffer, fb_pitch, mx + 20, ay - 5, mw - 40, 40, 5, 0xFF353545, fb_width, fb_height);
                }
                nxf_draw_text(framebuffer, fb_pitch, mx + 50, ay, actions[i], 24, color, fb_width, fb_height);
            }
        } else if (nucleus_modal_active == 2) {
            /* Format Confirm */
            nxf_draw_centered(framebuffer, fb_pitch, fb_width, my + 30, "Confirm Format", 32, 0xFFFF4040, fb_width, fb_height);
            nxf_draw_centered(framebuffer, fb_pitch, fb_width, my + 80, "This will erase all data!", 24, COLOR_WHITE, fb_width, fb_height);
            nxf_draw_centered(framebuffer, fb_pitch, fb_width, my + 110, "Are you sure?", 24, COLOR_WHITE, fb_width, fb_height);
            
            /* Buttons */
            int btn_y = my + 200;
            // Yes
            vic_rounded_rect_fill(framebuffer, fb_pitch, mx + 40, btn_y, 140, 50, 5, nucleus_modal_selection == 0 ? 0xFFFF4040 : 0xFF404040, fb_width, fb_height);
            nxf_draw_centered(framebuffer, fb_pitch, fb_width/2 - 80, btn_y + 15, "FORMAT", 24, COLOR_WHITE, fb_width, fb_height);
            // No
            vic_rounded_rect_fill(framebuffer, fb_pitch, mx + 220, btn_y, 140, 50, 5, nucleus_modal_selection == 1 ? COLOR_ACCENT : 0xFF404040, fb_width, fb_height);
            nxf_draw_centered(framebuffer, fb_pitch, fb_width/2 + 100, btn_y + 15, "Cancel", 24, COLOR_WHITE, fb_width, fb_height);
        }
    }
}

static void draw_docs(int page) {
     fb_clear(COLOR_PURPLE_BG);
     
     /* Adaptive font sizing */
     int scale = 1;
     if (fb_width >= 1920) scale = 2;
     else if (fb_width >= 1280) scale = 1;
     
     int font_title = 24 + scale * 8;
     int font_heading = 16 + scale * 6;
     int font_body = 12 + scale * 4;
     int font_small = 10 + scale * 3;
     int line_h = 22 + scale * 5;
     
     /* Header */
     fb_fill_rect(0, 0, fb_width, 50 + scale * 15, COLOR_PURPLE_BOX);
     nxf_draw_centered(framebuffer, fb_pitch, fb_width, 12 + scale * 6, "Documentation & Support", font_title, COLOR_WHITE, fb_width, fb_height);
     
     /* Content Box */
     int box_w = fb_width - 60 - scale * 40;
     int box_x = (fb_width - box_w)/2;
     int box_y = 55 + scale * 20;
     int box_h = fb_height - box_y - 70;
     
     vic_rounded_rect_fill(framebuffer, fb_pitch, box_x, box_y, box_w, box_h, 8, COLOR_PURPLE_BOX, fb_width, fb_height);
     
     int text_x = box_x + 15 + scale * 10;
     int text_y = box_y + 15 + scale * 8;
     
     if (page == 0) {
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "Welcome to NeolyxOS", font_heading, COLOR_ACCENT, fb_width, fb_height);
         text_y += line_h + 8;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "NeolyxOS: independent 64-bit OS.", font_body, COLOR_WHITE, fb_width, fb_height);
         text_y += line_h;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "Custom kernel, hybrid architecture.", font_body, COLOR_WHITE, fb_width, fb_height);
         text_y += line_h + 10;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "Navigation:", font_heading, COLOR_GRAY, fb_width, fb_height);
         text_y += line_h;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "- Arrow Keys to navigate", font_body, COLOR_WHITE, fb_width, fb_height);
         text_y += line_h;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "- Enter to select", font_body, COLOR_WHITE, fb_width, fb_height);
         text_y += line_h;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "- Esc to go back", font_body, COLOR_WHITE, fb_width, fb_height);
     } else if (page == 1) {
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "Troubleshooting", font_heading, COLOR_ACCENT, fb_width, fb_height);
         text_y += line_h + 8;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "If system fails to boot:", font_body, COLOR_WHITE, fb_width, fb_height);
         text_y += line_h;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "1. Try Safe Mode", font_body, COLOR_WHITE, fb_width, fb_height);
         text_y += line_h;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "2. Check Verbose Log", font_body, COLOR_WHITE, fb_width, fb_height);
         text_y += line_h;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "3. Use Recovery Shell", font_body, COLOR_WHITE, fb_width, fb_height);
     } else {
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "Contact Support", font_heading, COLOR_ACCENT, fb_width, fb_height);
         text_y += line_h + 8;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "Web: neolyxos.org", font_body, COLOR_WHITE, fb_width, fb_height);
         text_y += line_h;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "Email: support@neolyxos.org", font_body, COLOR_WHITE, fb_width, fb_height);
         text_y += line_h;
         nxf_draw_text(framebuffer, fb_pitch, text_x, text_y, "Version: 1.0.0-Alpha", font_body, COLOR_GRAY, fb_width, fb_height);
     }
     
     /* Pagination dots */
     int dot_sz = 5 + scale * 2;
     int dot_start = fb_width/2 - 20;
     for (int i=0; i<3; i++) {
         uint32_t color = (i == page) ? COLOR_WHITE : COLOR_GRAY;
         vic_rounded_rect_fill(framebuffer, fb_pitch, dot_start + i*15, fb_height - 85, dot_sz, dot_sz, dot_sz/2, color, fb_width, fb_height);
     }

     nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 35, "[Left/Right] Page   [Esc] Back", font_small, 0x00A0A0B0, fb_width, fb_height);
}

static void draw_about_page(void) {
     fb_clear(COLOR_PURPLE_BG);
     
     /* Central Logo */
     // TODO: Vector logo
     nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 100, "NeolyxOS", 80, COLOR_WHITE, fb_width, fb_height);
     
     nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20, "Version 1.0.0 Alpha", 32, COLOR_ACCENT, fb_width, fb_height);
     nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 60, "Copyright (c) 2025 KetiveeAI", 24, COLOR_GRAY, fb_width, fb_height);
     
     nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 60, "[Esc] Back", 24, 0x00A0A0B0, fb_width, fb_height);
}

/* Install flow state */
static int install_os_type = 0;  /* 0=Desktop, 1=Server */
static int install_disk_idx = -1; /* Selected disk for install */
static int install_progress = 0;  /* 0-100 progress */

/* Check if a partition has NXFS filesystem (compatible for install) */
static int is_disk_compatible(int disk_idx) {
    /* A disk is compatible if it has at least one NXFS partition */
    for (int i = 0; i < nucleus_partition_count; i++) {
        /* Check if partition belongs to this disk and has NXFS */
        if (nucleus_partitions[i].disk_index == disk_idx) {
            /* Compare filesystem type - case insensitive check for "NXFS" */
            const char *fs = nucleus_partitions[i].fs_type;
            if ((fs[0] == 'N' || fs[0] == 'n') &&
                (fs[1] == 'X' || fs[1] == 'x') &&
                (fs[2] == 'F' || fs[2] == 'f') &&
                (fs[3] == 'S' || fs[3] == 's')) {
                return 1;
            }
        }
    }
    return 0;
}

/* Count compatible disks */
static int count_compatible_disks(void) {
    int count = 0;
    for (int i = 0; i < nucleus_disk_count; i++) {
        if (is_disk_compatible(i)) count++;
    }
    return count;
}

/* Draw OS Selection screen (Server vs Desktop) */
static void draw_os_select(int selection) {
    fb_clear(COLOR_PURPLE_BG);
    
    /* Header */
    fb_fill_rect(0, 0, fb_width, 80, COLOR_PURPLE_BOX);
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 25, "Select Installation Type", 48, COLOR_WHITE, fb_width, fb_height);
    
    /* Options */
    uint32_t box_w = 350;
    uint32_t box_h = 200;
    uint32_t gap = 60;
    uint32_t start_x = fb_width / 2 - box_w - gap / 2;
    uint32_t start_y = fb_height / 2 - box_h / 2;
    
    /* Desktop option */
    uint32_t desk_color = (selection == 0) ? COLOR_PURPLE_LIGHT : COLOR_PURPLE_BOX;
    vic_rounded_rect_fill(framebuffer, fb_pitch, start_x, start_y, box_w, box_h, 15, desk_color, fb_width, fb_height);
    if (selection == 0) {
        vic_rounded_rect(framebuffer, fb_pitch, start_x, start_y, box_w, box_h, 15, 3, COLOR_ACCENT, fb_width, fb_height);
    }
    vic_render_icon(framebuffer, fb_pitch, start_x + box_w/2 - 32, start_y + 30, 64, icon_neolyx_v2, COLOR_WHITE, fb_width, fb_height);
    nxf_draw_centered(framebuffer, fb_pitch, start_x + box_w/2, start_y + 110, "Desktop", 32, COLOR_WHITE, fb_width, fb_height);
    nxf_draw_centered(framebuffer, fb_pitch, start_x + box_w/2, start_y + 150, "Full GUI experience", 18, COLOR_GRAY, fb_width, fb_height);
    nxf_draw_centered(framebuffer, fb_pitch, start_x + box_w/2, start_y + 175, "Recommended", 16, COLOR_ACCENT, fb_width, fb_height);
    
    /* Server option */
    uint32_t srv_x = start_x + box_w + gap;
    uint32_t srv_color = (selection == 1) ? COLOR_PURPLE_LIGHT : COLOR_PURPLE_BOX;
    vic_rounded_rect_fill(framebuffer, fb_pitch, srv_x, start_y, box_w, box_h, 15, srv_color, fb_width, fb_height);
    if (selection == 1) {
        vic_rounded_rect(framebuffer, fb_pitch, srv_x, start_y, box_w, box_h, 15, 3, COLOR_ACCENT, fb_width, fb_height);
    }
    vic_render_icon(framebuffer, fb_pitch, srv_x + box_w/2 - 32, start_y + 30, 64, icon_terminal_v2, COLOR_WHITE, fb_width, fb_height);
    nxf_draw_centered(framebuffer, fb_pitch, srv_x + box_w/2, start_y + 110, "Server", 32, COLOR_WHITE, fb_width, fb_height);
    nxf_draw_centered(framebuffer, fb_pitch, srv_x + box_w/2, start_y + 150, "CLI minimal install", 18, COLOR_GRAY, fb_width, fb_height);
    nxf_draw_centered(framebuffer, fb_pitch, srv_x + box_w/2, start_y + 175, "Advanced users", 16, 0x00808090, fb_width, fb_height);
    
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 60, "[Left/Right] Select   [Enter] Continue   [Esc] Back", 24, 0x00A0A0B0, fb_width, fb_height);
}

/* Draw Disk Selection screen with compatibility check */
static void draw_disk_select(int selection) {
    fb_clear(COLOR_PURPLE_BG);
    
    /* Header */
    fb_fill_rect(0, 0, fb_width, 80, COLOR_PURPLE_BOX);
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 25, "Select Installation Disk", 48, COLOR_WHITE, fb_width, fb_height);
    
    int compatible_count = count_compatible_disks();
    
    if (compatible_count == 0) {
        /* No compatible disks - show warning */
        uint32_t box_x = fb_width / 2 - 280;
        uint32_t box_y = fb_height / 2 - 100;
        
        vic_rounded_rect_fill(framebuffer, fb_pitch, box_x, box_y, 560, 200, 15, 0xFF3A2020, fb_width, fb_height);
        vic_rounded_rect(framebuffer, fb_pitch, box_x, box_y, 560, 200, 15, 3, 0xFFFF4040, fb_width, fb_height);
        
        vic_render_icon(framebuffer, fb_pitch, fb_width/2 - 32, box_y + 20, 64, icon_warning_v2, 0xFFFF4040, fb_width, fb_height);
        
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, box_y + 100, "No compatible disks found!", 28, COLOR_WHITE, fb_width, fb_height);
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, box_y + 135, "Format a disk with NXFS using Nucleus Disk first.", 20, COLOR_GRAY, fb_width, fb_height);
        
        /* Button to go to Nucleus */
        uint32_t btn_x = fb_width / 2 - 120;
        uint32_t btn_y = box_y + 160;
        vic_rounded_rect_fill(framebuffer, fb_pitch, btn_x, btn_y, 240, 45, 8, COLOR_ACCENT, fb_width, fb_height);
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, btn_y + 12, "Open Nucleus Disk", 20, COLOR_WHITE, fb_width, fb_height);
        
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 60, "[Enter] Go to Nucleus Disk   [Esc] Back", 24, 0x00A0A0B0, fb_width, fb_height);
    } else {
        /* Show disk list */
        uint32_t panel_w = fb_width - 200;
        uint32_t panel_x = 100;
        uint32_t panel_y = 120;
        
        nxf_draw_text(framebuffer, fb_pitch, panel_x, panel_y, "Select a disk to install NeolyxOS:", 24, COLOR_WHITE, fb_width, fb_height);
        panel_y += 50;
        
        int current_sel = 0;
        for (int i = 0; i < nucleus_disk_count && i < 6; i++) {
            int compatible = is_disk_compatible(i);
            uint32_t item_y = panel_y + i * 70;
            
            /* Background - grayed out if not compatible */
            if (compatible) {
                if (current_sel == selection) {
                    vic_rounded_rect_fill(framebuffer, fb_pitch, panel_x, item_y, panel_w, 60, 10, COLOR_PURPLE_LIGHT, fb_width, fb_height);
                    vic_rounded_rect(framebuffer, fb_pitch, panel_x, item_y, panel_w, 60, 10, 2, COLOR_ACCENT, fb_width, fb_height);
                } else {
                    vic_rounded_rect_fill(framebuffer, fb_pitch, panel_x, item_y, panel_w, 60, 10, COLOR_PURPLE_BOX, fb_width, fb_height);
                }
                current_sel++;
            } else {
                /* Incompatible - blurred/grayed */
                vic_rounded_rect_fill(framebuffer, fb_pitch, panel_x, item_y, panel_w, 60, 10, 0xFF252530, fb_width, fb_height);
            }
            
            /* Icon */
            uint32_t icon_col = compatible ? (current_sel-1 == selection ? COLOR_ACCENT : COLOR_GRAY) : 0xFF404040;
            vic_render_icon(framebuffer, fb_pitch, panel_x + 15, item_y + 14, 32, icon_disk_v2, icon_col, fb_width, fb_height);
            
            /* Disk name */
            uint32_t text_col = compatible ? COLOR_WHITE : 0xFF606060;
            nxf_draw_text(framebuffer, fb_pitch, panel_x + 60, item_y + 10, nucleus_disks[i].name, 22, text_col, fb_width, fb_height);
            
            /* Model */
            nxf_draw_text(framebuffer, fb_pitch, panel_x + 60, item_y + 35, nucleus_disks[i].model, 16, compatible ? COLOR_GRAY : 0xFF505050, fb_width, fb_height);
            
            /* Size */
            char size_s[16];
            nucleus_format_size(nucleus_disks[i].size_bytes, size_s, 16);
            nxf_draw_text(framebuffer, fb_pitch, panel_x + 400, item_y + 20, size_s, 20, compatible ? COLOR_WHITE : 0xFF505050, fb_width, fb_height);
            
            /* Status badge */
            if (!compatible) {
                nxf_draw_text(framebuffer, fb_pitch, panel_x + panel_w - 150, item_y + 20, "Not Compatible", 16, 0xFFFF4040, fb_width, fb_height);
            } else if (nucleus_disks[i].is_boot_disk) {
                nxf_draw_text(framebuffer, fb_pitch, panel_x + panel_w - 100, item_y + 20, "Boot Disk", 16, COLOR_GREEN, fb_width, fb_height);
            }
        }
        
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 60, "[Up/Down] Select   [Enter] Install   [Esc] Back", 24, 0x00A0A0B0, fb_width, fb_height);
    }
}

/* Draw Install Progress screen */
static void draw_install_progress(void) {
    fb_clear(COLOR_PURPLE_BG);
    
    /* Header */
    fb_fill_rect(0, 0, fb_width, 80, COLOR_PURPLE_BOX);
    const char *os_name = (install_os_type == 0) ? "NeolyxOS Desktop" : "NeolyxOS Server";
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 25, "Installing...", 48, COLOR_WHITE, fb_width, fb_height);
    
    /* Center content */
    uint32_t cx = fb_width / 2;
    uint32_t cy = fb_height / 2;
    
    /* OS Type being installed */
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, cy - 80, os_name, 32, COLOR_ACCENT, fb_width, fb_height);
    
    /* Target disk */
    if (install_disk_idx >= 0 && install_disk_idx < nucleus_disk_count) {
        char disk_info[64];
        nx_strcpy(disk_info, "Target: ", 64);
        /* Append disk name manually */
        int dlen = 8; /* "Target: " */
        for (int k = 0; nucleus_disks[install_disk_idx].name[k] && dlen < 60; k++) {
            disk_info[dlen++] = nucleus_disks[install_disk_idx].name[k];
        }
        disk_info[dlen] = '\0';
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, cy - 40, disk_info, 20, COLOR_GRAY, fb_width, fb_height);
    }
    
    /* Progress bar */
    uint32_t bar_w = 500;
    uint32_t bar_h = 30;
    uint32_t bar_x = cx - bar_w / 2;
    uint32_t bar_y = cy + 20;
    
    /* Bar background */
    vic_rounded_rect_fill(framebuffer, fb_pitch, bar_x, bar_y, bar_w, bar_h, 8, 0xFF303040, fb_width, fb_height);
    
    /* Bar fill */
    uint32_t fill_w = (bar_w * install_progress) / 100;
    if (fill_w > 0) {
        vic_rounded_rect_fill(framebuffer, fb_pitch, bar_x, bar_y, fill_w, bar_h, 8, COLOR_ACCENT, fb_width, fb_height);
    }
    
    /* Border */
    vic_rounded_rect(framebuffer, fb_pitch, bar_x, bar_y, bar_w, bar_h, 8, 2, COLOR_BORDER, fb_width, fb_height);
    
    /* Percentage text */
    char pct_str[8] = "0%";
    if (install_progress < 10) {
        pct_str[0] = '0' + install_progress;
        pct_str[1] = '%';
        pct_str[2] = '\0';
    } else if (install_progress < 100) {
        pct_str[0] = '0' + (install_progress / 10);
        pct_str[1] = '0' + (install_progress % 10);
        pct_str[2] = '%';
        pct_str[3] = '\0';
    } else {
        pct_str[0] = '1'; pct_str[1] = '0'; pct_str[2] = '0'; pct_str[3] = '%'; pct_str[4] = '\0';
    }
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, bar_y + 5, pct_str, 20, COLOR_WHITE, fb_width, fb_height);
    
    /* Status text */
    const char *status_msg = "Preparing installation...";
    if (install_progress >= 10 && install_progress < 30) status_msg = "Copying system files...";
    else if (install_progress >= 30 && install_progress < 60) status_msg = "Installing kernel and drivers...";
    else if (install_progress >= 60 && install_progress < 80) status_msg = "Configuring system...";
    else if (install_progress >= 80 && install_progress < 100) status_msg = "Finalizing installation...";
    else if (install_progress >= 100) status_msg = "Installation complete!";
    
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, bar_y + 60, status_msg, 20, COLOR_GRAY, fb_width, fb_height);
    
    /* Estimated time (simulated) */
    int remaining = (100 - install_progress) / 5;  /* ~5% per "unit" */
    if (install_progress < 100) {
        char time_str[32] = "Estimated time: ";
        int tlen = 16;
        if (remaining >= 10) {
            time_str[tlen++] = '0' + (remaining / 10);
        }
        time_str[tlen++] = '0' + (remaining % 10);
        time_str[tlen++] = ' ';
        time_str[tlen++] = 'm';
        time_str[tlen++] = 'i';
        time_str[tlen++] = 'n';
        time_str[tlen] = '\0';
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, bar_y + 90, time_str, 18, 0x00707080, fb_width, fb_height);
    }
}

void kernel_main(NeolyxBootInfo *boot_info) {

    serial_init();
    serial_puts("\n\n========================================\n");
    serial_puts("    NeolyxOS Boot Manager\n");
    serial_puts("========================================\n\n");
    
    g_boot_info = boot_info;  /* Save for kernel */
    
    if (!boot_info || boot_info->magic != NEOLYX_BOOT_MAGIC) {
        serial_puts("[ERROR] Invalid boot info!\n");
        goto halt_loop;
    }
    
    if (boot_info->framebuffer_addr && boot_info->framebuffer_width > 0) {
        framebuffer = (volatile uint32_t*)boot_info->framebuffer_addr;
        fb_width = boot_info->framebuffer_width;
        fb_height = boot_info->framebuffer_height;
        fb_pitch = boot_info->framebuffer_pitch;
        
        serial_puts("[OK] Framebuffer initialized\n");
        
        /* Detect VM vs physical hardware for optimization */
        detect_platform();
        
        /* Navigation state */
        int selected = 0;      /* Current selection within panel */
        int in_sidebar = 0;    /* 0 = main panel, 1 = sidebar */
        
        /* Initialize mouse */
        mouse_init();
        mouse_x = fb_width / 2;
        mouse_y = fb_height / 2;
        
        /* Initialize NUCLEUS Disk Manager (scan disks) */
        nucleus_scan_disks();
        if (nucleus_disk_count > 0) {
            nucleus_load_partitions(0); /* Load first disk */
        }
        
        /* Initial draw */
        draw_boot_manager(selected, in_sidebar);
        cursor_draw();
        serial_puts("[OK] Boot Manager displayed\n");
        
        while (1) {
            int needs_redraw = 0;

            int8_t dx, dy;
            uint8_t buttons;
            if (mouse_poll(&dx, &dy, &buttons)) {
                cursor_restore_bg();
                
                /* Calculate sensitivity based on screen resolution */
                /* Higher resolution = more movement needed */
                /* Target: 1920x1080 is the baseline (scale = 1.0) */
                int scale_x = fb_width / 800;   /* 800 = base reference */
                int scale_y = fb_height / 600;  /* 600 = base reference */
                if (scale_x < 1) scale_x = 1;
                if (scale_y < 1) scale_y = 1;
                if (scale_x > 3) scale_x = 3;   /* Cap at 3x */
                if (scale_y > 3) scale_y = 3;
                
                /* Apply deadzone (ignore jitter) */
                if (dx > -3 && dx < 3) dx = 0;
                if (dy > -3 && dy < 3) dy = 0;
                
                /* Apply smooth acceleration (small moves = precise, large moves = fast) */
                int32_t move_x = dx * scale_x;
                int32_t move_y = dy * scale_y;
                
                /* Clamp maximum movement per poll to prevent jumping */
                int max_move = 15 * scale_x;  /* Scale max with resolution */
                if (move_x > max_move) move_x = max_move;
                if (move_x < -max_move) move_x = -max_move;
                if (move_y > max_move) move_y = max_move;
                if (move_y < -max_move) move_y = -max_move;
                
                /* Update position */
                mouse_x += move_x;
                mouse_y += move_y;
                
                /* Clamp to screen bounds */
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x >= (int32_t)fb_width) mouse_x = fb_width - 1;
                if (mouse_y >= (int32_t)fb_height) mouse_y = fb_height - 1;
                
                /* Check for click (left button) */
                if ((buttons & 0x01) && !(mouse_buttons & 0x01)) {
                    int click_activated = 0;  /* Set if we should activate selection */
                    
                    /* Detect which item was clicked */
                    uint32_t main_x = fb_width / 4 - 100;
                    uint32_t main_y = 140;
                    uint32_t main_w = 280;
                    uint32_t side_x = fb_width * 3 / 4 - 80;
                    uint32_t side_y = 140;
                    uint32_t side_w = 180;
                    
                    /* Check main menu clicks */
                    for (int i = 0; i < MENU_MAIN_COUNT; i++) {
                        uint32_t item_y = main_y + 10 + i * 55;
                        if (mouse_x >= (int32_t)main_x && mouse_x < (int32_t)(main_x + main_w) &&
                            mouse_y >= (int32_t)item_y && mouse_y < (int32_t)(item_y + 50)) {
                            /* If clicking already-selected item, activate it */
                            if (in_sidebar == 0 && selected == i) {
                                click_activated = 1;
                            } else {
                                in_sidebar = 0;
                                selected = i;
                                needs_redraw = 1;
                            }
                            break;
                        }
                    }
                    
                    /* Check sidebar clicks */
                    for (int i = 0; i < MENU_SIDEBAR_COUNT; i++) {
                        uint32_t item_y = side_y + 10 + i * 35;
                        if (mouse_x >= (int32_t)side_x && mouse_x < (int32_t)(side_x + side_w) &&
                            mouse_y >= (int32_t)item_y && mouse_y < (int32_t)(item_y + 30)) {
                            /* If clicking already-selected item, activate it */
                            if (in_sidebar == 1 && selected == i) {
                                click_activated = 1;
                            } else {
                                in_sidebar = 1;
                                selected = i;
                                needs_redraw = 1;
                            }
                            break;
                        }
                    }
                    
                    /* If click activated, simulate Enter key */
                    if (click_activated) {
                        /* Inject fake Enter scancode for next loop iteration */
                        /* We'll handle this by setting a flag */
                        mouse_buttons = buttons;
                        /* Directly trigger Enter action here */
                        goto handle_enter;
                    }
                }
                mouse_buttons = buttons;
                
                /* Only redraw cursor if it actually moved */
                cursor_draw();
            }
            
            /* Poll keyboard */
            uint8_t scancode = get_key();
            if (scancode && !(scancode & 0x80)) {
            
            int max_items = in_sidebar ? MENU_SIDEBAR_COUNT : MENU_MAIN_COUNT;
            
            switch (scancode) {
                case 0x48: /* Up arrow */
                    if (current_view == VIEW_MAIN) {
                        if (selected > 0) {
                            selected--;
                            needs_redraw = 1;
                        }
                    } else if (current_view == VIEW_BOOT_OPTIONS) {
                         if (sub_selection > 0) {
                            sub_selection--;
                            needs_redraw = 1;
                         }
                    } else if (current_view == VIEW_NUCLEUS_DISK) {
                        if (nucleus_modal_active == 0) {
                            if (sub_selection > 0) {
                                sub_selection--;
                                nucleus_load_partitions(sub_selection);
                                needs_redraw = 1;
                            }
                        } else if (nucleus_modal_active == 1) {
                            if (nucleus_modal_selection > 0) {
                                nucleus_modal_selection--;
                                needs_redraw = 1;
                            }
                        } else if (nucleus_modal_active == 2) {
                            if (nucleus_modal_selection > 0) {
                                nucleus_modal_selection--;
                                needs_redraw = 1;
                            }
                        }
                    }
                    break;
                    
                case 0x50: /* Down arrow */
                     if (current_view == VIEW_MAIN) {
                        if (selected < max_items - 1) {
                            selected++;
                            needs_redraw = 1;
                        }
                     } else if (current_view == VIEW_BOOT_OPTIONS) {
                         if (sub_selection < 4) { /* 5 options */
                            sub_selection++;
                            needs_redraw = 1;
                         }
                    } else if (current_view == VIEW_NUCLEUS_DISK) {
                        if (nucleus_modal_active == 0) {
                            if (sub_selection < nucleus_disk_count - 1) {
                                sub_selection++;
                                nucleus_load_partitions(sub_selection);
                                needs_redraw = 1;
                            }
                        } else if (nucleus_modal_active == 1) {
                             if (nucleus_modal_selection < 3) { /* 4 items */
                                nucleus_modal_selection++;
                                needs_redraw = 1;
                             }
                        } else if (nucleus_modal_active == 2) {
                             if (nucleus_modal_selection < 1) { /* 2 buttons */
                                nucleus_modal_selection++;
                                needs_redraw = 1;
                             }
                        }
                    } else if (current_view == VIEW_DISK_SELECT) {
                        int compat = count_compatible_disks();
                        if (sub_selection < compat - 1) {
                            sub_selection++;
                            needs_redraw = 1;
                        }
                    }
                    break;
                    
                case 0x4B: /* Left arrow */
                    if (current_view == VIEW_MAIN) {
                         if (in_sidebar) {
                            in_sidebar = 0;
                            selected = 0;
                            needs_redraw = 1;
                        }
                    } else if (current_view == VIEW_DOCS) {
                        if (sub_selection > 0) {
                            sub_selection--;
                            needs_redraw = 1;
                        }
                    } else if (current_view == VIEW_OS_SELECT) {
                        if (sub_selection > 0) {
                            sub_selection--;
                            needs_redraw = 1;
                        }
                    }
                    break;
                    
                case 0x4D: /* Right arrow */
                    if (current_view == VIEW_MAIN) {
                        if (!in_sidebar) {
                            in_sidebar = 1;
                            selected = 0;
                            needs_redraw = 1;
                        }
                    } else if (current_view == VIEW_DOCS) {
                        if (sub_selection < 2) { /* 3 pages */
                            sub_selection++;
                            needs_redraw = 1;
                        }
                    } else if (current_view == VIEW_OS_SELECT) {
                        if (sub_selection < 1) { /* 2 options: Desktop/Server */
                            sub_selection++;
                            needs_redraw = 1;
                        }
                    }
                    break;
                    
                case 0x01: /* Esc */
                    if (current_view != VIEW_MAIN) {
                        if (current_view == VIEW_NUCLEUS_DISK && nucleus_modal_active > 0) {
                            /* Close modal, stay in Nucleus */
                            nucleus_modal_active = 0;
                            needs_redraw = 1;
                        } else {
                            /* Return to main menu */
                            current_view = VIEW_MAIN;
                            needs_redraw = 1;
                            cursor_restore_bg(); /* Ensure cursor clean up */
                        }
                    }
                    else if (current_view == VIEW_MAIN && in_sidebar) {
                         /* Return to main menu from sidebar */
                         in_sidebar = 0;
                         selected = 0;
                         needs_redraw = 1;
                    }
                    break;

                handle_enter:

                case 0x1C: /* Enter */
                    if (current_view == VIEW_BOOT_OPTIONS) {
                         /* Toggle option */
                         uint8_t *opt = get_boot_opt_ptr(sub_selection);
                         if (opt) *opt = !*opt;
                         needs_redraw = 1;
                    } 
                    else if (current_view == VIEW_NUCLEUS_DISK) {
                        if (nucleus_modal_active == 0) {
                            /* Open Actions Menu */
                            nucleus_modal_active = 1;
                            nucleus_modal_selection = 0;
                            needs_redraw = 1;
                        } else if (nucleus_modal_active == 1) {
                            /* Execute Action */
                            if (nucleus_modal_selection == 0) { /* Format */
                                nucleus_modal_active = 2; /* Confirm */
                                nucleus_modal_selection = 1; /* Default to No */
                                needs_redraw = 1;
                            } else {
                                /* TODO: Implement other actions */
                                nucleus_modal_active = 0;
                                needs_redraw = 1;
                            }
                        } else if (nucleus_modal_active == 2) {
                            /* Confirm Format */
                            if (nucleus_modal_selection == 0) { /* Yes */
                                fb_clear(COLOR_PURPLE_BG);
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2, "Formatting Disk...", 32, COLOR_WHITE, fb_width, fb_height);
                                for (volatile int i = 0; i < 50000000; i++);
                                nucleus_modal_active = 0;
                                needs_redraw = 1;
                            } else { /* No */
                                nucleus_modal_active = 0;
                                needs_redraw = 1;
                            }
                        }
                    }
                    else if (current_view == VIEW_OS_SELECT) {
                        /* Save OS type and go to Disk Selection */
                        install_os_type = sub_selection;  /* 0=Desktop, 1=Server */
                        current_view = VIEW_DISK_SELECT;
                        sub_selection = 0;
                        needs_redraw = 1;
                    }
                    else if (current_view == VIEW_DISK_SELECT) {
                        int compatible_count = count_compatible_disks();
                        if (compatible_count == 0) {
                            /* No compatible disks - go to Nucleus Disk */
                            current_view = VIEW_NUCLEUS_DISK;
                            sub_selection = 0;
                            needs_redraw = 1;
                        } else {
                            /* Find the actual disk index from compatible selection */
                            int sel_idx = 0;
                            for (int i = 0; i < nucleus_disk_count; i++) {
                                if (is_disk_compatible(i)) {
                                    if (sel_idx == sub_selection) {
                                        install_disk_idx = i;
                                        break;
                                    }
                                    sel_idx++;
                                }
                            }
                            /* Start installation (simulated) */
                            install_progress = 0;
                            current_view = VIEW_INSTALL_PROGRESS;
                            needs_redraw = 1;
                            
                            /* Simulate installation progress */
                            for (install_progress = 0; install_progress <= 100; install_progress += 5) {
                                draw_install_progress();
                                for (volatile int d = 0; d < 20000000; d++);
                            }
                            
                            /* Installation complete - show success */
                            fb_clear(COLOR_PURPLE_BG);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 40, "Installation Complete!", 48, COLOR_GREEN, fb_width, fb_height);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20, "NeolyxOS has been installed successfully.", 24, COLOR_WHITE, fb_width, fb_height);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 60, "Restart to boot into your new system.", 20, COLOR_GRAY, fb_width, fb_height);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 60, "Press any key to return to menu...", 20, 0x00808090, fb_width, fb_height);
                            wait_key();
                            
                            /* Return to main menu */
                            current_view = VIEW_MAIN;
                            needs_redraw = 1;
                        }
                    }
                    else if (current_view == VIEW_MAIN) {
                        if (!in_sidebar) {
                            /* Main menu selection */

                        serial_puts("Selected: ");
                        serial_puts(menu_main_labels[selected]);
                        serial_puts("\n");
                        
                        if (selected == 0) { /* Boot NeolyxOS */
                            fb_clear(COLOR_PURPLE_BG);
                            fb_draw_centered(fb_height/2 - 20, "Booting NeolyxOS...", COLOR_WHITE);
                            if (g_boot_options.safe_mode) {
                                fb_draw_centered(fb_height/2 + 10, "Safe Mode enabled", COLOR_YELLOW);
                            } else if (g_boot_options.verbose_boot) {
                                fb_draw_centered(fb_height/2 + 10, "Verbose Boot enabled", COLOR_GRAY);
                            } else {
                                fb_draw_centered(fb_height/2 + 10, "Loading full kernel...", COLOR_GRAY);
                            }
                            serial_puts("[BOOT] Launching full kernel...\n");
                            
                            /* Launch the full NeolyxOS kernel */
                            nx_kernel_start();
                            
                            /* If kernel returns, go back to menu */
                            draw_boot_manager(selected, in_sidebar);
                            break;
                        }
                        
                        if (selected == 1) { /* Boot Options */
                            current_view = VIEW_BOOT_OPTIONS;
                            sub_selection = 0;
                            needs_redraw = 1;
                            break;
                        }
                        
                        if (selected == 4) { /* Nucleus Disk */
                            current_view = VIEW_NUCLEUS_DISK;
                            sub_selection = 0;
                            needs_redraw = 1;
                            break;
                        }
                        
                        if (selected == 2) { /* Install NeolyxOS */
                            /* Go to OS Selection screen */
                            current_view = VIEW_OS_SELECT;
                            sub_selection = 0;  /* Default to Desktop */
                            needs_redraw = 1;
                            break;
                        }
                        
                        if (selected == 3) { /* Restore System */
                            fb_clear(COLOR_PURPLE_BG);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2, "System Restore not available", 24, COLOR_WHITE, fb_width, fb_height);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 30, "Press any key to return...", 20, 0x00808090, fb_width, fb_height);
                            wait_key();
                            draw_boot_manager(selected, in_sidebar);
                            break;
                        }
                        
                        /* Show feature screen for other options */
                        fb_clear(COLOR_PURPLE_BG);
                        nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 20, menu_main_labels[selected], 24, COLOR_WHITE, fb_width, fb_height);
                        nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20, "Press any key to return...", 20, 0x00808090, fb_width, fb_height);
                        wait_key();
                        draw_boot_manager(selected, in_sidebar);
                        
                        
                    } else {
                        /* Sidebar selection */
                        serial_puts("Sidebar: ");
                        serial_puts(menu_sidebar_labels[selected]);
                        serial_puts("\n");
                        
                        if (selected == 0) { /* Exit NeolyxOS */
                            fb_clear(COLOR_PURPLE_BG);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2, "Shutting down...", 24, COLOR_WHITE, fb_width, fb_height);
                            serial_puts("[POWER] Initiating shutdown...\n");
                            
                            /* ACPI S5 power off (QEMU) */
                            outw(0x604, 0x2000);
                            
                            /* Fallback: keyboard controller reset */
                            for (volatile int i = 0; i < 1000000; i++);
                            outb(0x64, 0xFE);
                            
                            goto halt_loop;
                        }
                        
                        if (selected == 1) { /* UEFI Settings */
                            fb_clear(COLOR_PURPLE_BG);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 60, "Rebooting to UEFI Setup...", 24, COLOR_WHITE, fb_width, fb_height);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 20, "After restart, press one of:", 20, COLOR_GRAY, fb_width, fb_height);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 10, "DEL | F2 | F10 | F12 | ESC", 24, COLOR_ACCENT, fb_width, fb_height);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 50, "to enter firmware setup", 20, 0x00808090, fb_width, fb_height);
                            serial_puts("[UEFI] Rebooting for firmware setup...\n");
                            
                            /* Give user time to read */
                            for (volatile int i = 0; i < 50000000; i++);
                            
                            /* Perform reboot */
                            outw(0x604, 0x2000);  /* QEMU ACPI reboot */
                            outb(0x64, 0xFE);    /* Keyboard controller reset */
                            goto halt_loop;
                        }
                        
                        if (selected == 2) { /* Terminal */
                            fb_clear(COLOR_PURPLE_BG);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2, "Terminal not available", 24, COLOR_WHITE, fb_width, fb_height);
                            wait_key();
                            draw_boot_manager(selected, in_sidebar);
                            break;
                        }
                        
                        if (selected == 3) { /* Support/Docs */
                            current_view = VIEW_DOCS;
                            sub_selection = 0;
                            needs_redraw = 1;
                            break;
                        }
                        
                        if (selected == 4) { /* Recovery Shell */
                            fb_clear(COLOR_PURPLE_BG);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2, "Recovery Shell not available", 24, COLOR_WHITE, fb_width, fb_height);
                            wait_key();
                            draw_boot_manager(selected, in_sidebar);
                            break;
                        }
                        
                        if (selected == 5) { /* Zepra Browser */
                            fb_clear(COLOR_PURPLE_BG);
                            
                            /* Header */
                            fb_fill_rect(0, 0, fb_width, 60, COLOR_PURPLE_BOX);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, 35, "Zepra Browser", 24, COLOR_WHITE, fb_width, fb_height);
                            
                            /* Info box */
                            uint32_t box_x = fb_width / 2 - 220;
                            uint32_t box_y = fb_height / 2 - 80;
                            draw_box_rounded(box_x, box_y, 440, 160, COLOR_PURPLE_BOX);
                            
                            nxf_draw_text(framebuffer, fb_pitch, box_x + 20, box_y + 20, "Zepra Browser is not available in", 20, COLOR_WHITE, fb_width, fb_height);
                            nxf_draw_text(framebuffer, fb_pitch, box_x + 20, box_y + 45, "the Boot Manager environment.", 20, COLOR_WHITE, fb_width, fb_height);
                            
                            nxf_draw_text(framebuffer, fb_pitch, box_x + 20, box_y + 80, "To use Zepra Browser:", 20, COLOR_ACCENT, fb_width, fb_height);
                            nxf_draw_text(framebuffer, fb_pitch, box_x + 35, box_y + 105, "1. Boot NeolyxOS (full desktop)", 20, 0x00A0A0B0, fb_width, fb_height);
                            nxf_draw_text(framebuffer, fb_pitch, box_x + 35, box_y + 125, "2. Launch from Applications menu", 20, 0x00A0A0B0, fb_width, fb_height);
                            
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 50, "Press any key to return...", 20, 0x00505060, fb_width, fb_height);
                            
                            wait_key();
                            draw_boot_manager(selected, in_sidebar);
                            break;
                        }
                        
                        if (selected == 6) { /* About */
                            current_view = VIEW_ABOUT;
                            needs_redraw = 1;
                            break;
                        }
                        
                        /* Show feature screen for others */
                        fb_clear(COLOR_PURPLE_BG);
                        nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 20, menu_sidebar_labels[selected], 24, COLOR_WHITE, fb_width, fb_height);
                        nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20, "Press any key to return...", 20, 0x00808090, fb_width, fb_height);
                        wait_key();
                        draw_boot_manager(selected, in_sidebar);
                    }
                    } /* close else if (current_view == VIEW_MAIN) */
                    break;
            }

            } /* end if(scancode) */
            
            /* Redraw dispatcher - handles ALL views */
            if (needs_redraw) {
                cursor_restore_bg();
                switch(current_view) {
                    case VIEW_MAIN:
                        draw_boot_manager(selected, in_sidebar);
                        break;
                    case VIEW_BOOT_OPTIONS:
                        draw_boot_options(sub_selection);
                        break;
                    case VIEW_DOCS:
                        draw_docs(sub_selection);
                        break;
                    case VIEW_ABOUT:
                        draw_about_page();
                        break;
                    case VIEW_NUCLEUS_DISK:
                        draw_nucleus_disk(sub_selection);
                        break;
                    case VIEW_OS_SELECT:
                        draw_os_select(sub_selection);
                        break;
                    case VIEW_DISK_SELECT:
                        draw_disk_select(sub_selection);
                        break;
                    case VIEW_INSTALL_PROGRESS:
                        draw_install_progress();
                        break;
                }
                if (mouse_initialized) cursor_draw();
            }
            
            /* Short delay to prevent 100% CPU - but allow mouse polling */
            for (volatile int i = 0; i < 10000; i++);
        }
    } else {
        serial_puts("[ERROR] No framebuffer available\n");
    }
    
halt_loop:
    serial_puts("\n[HALT] System halted.\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}
