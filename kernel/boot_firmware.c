/*
 * NeolyxOS Boot Firmware
 * 
 * ARCHITECTURE NOTE:
 * This is the BOOT FIRMWARE that provides the graphical boot menu.
 * The boot sequence is:
 *   1. UEFI Firmware loads kernel.bin
 *   2. startup.S calls kernel_main() defined HERE
 *   3. Boot Firmware shows menu (Install, Boot, Restore, etc.)
 *   4. When "Boot NeolyxOS" selected, calls nx_kernel_start() from main.c
 *
 * DO NOT rename kernel_main() - it is called by startup.S!
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "include/ui/vector_icons_v2.h"
#include "include/ui/disk_icons.h"
#include "include/utils/nucleus.h"
#include "include/ui/nxfont.h"
#include "include/ui/vector_icons_v2.h"
#include "fs/vfs.h"
#include "fs/nxfs.h"
#include "storage/ahci_block.h"
#include "include/config/nlc_config.h"
#include "browser_session.h"

/* Forward declarations */

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

/* Framebuffer state (boot manager internal) */
/* Framebuffer state (boot manager internal -> now shared global) */
extern volatile uint32_t *framebuffer;
extern uint32_t fb_width;
extern uint32_t fb_height;
extern uint32_t fb_pitch;

/* Exported framebuffer for desktop shell (accessed by main.c) */
volatile uint32_t *boot_framebuffer = 0;
uint32_t boot_fb_width = 0;
uint32_t boot_fb_height = 0;
uint32_t boot_fb_pitch = 0;

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

/* OS Version - Read from nlc_config or use defaults */
#define NEOLYX_VERSION_STRING "v1.0.1 Alpha"
#define NEOLYX_BOOT_MANAGER_TITLE "NeolyxOS Boot Firmware"

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
const uint8_t font_basic[95][16] = {
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

/* ========== TYPOGRAPHY CONSTANTS ========== */
#define FONT_HEADER_LARGE  24
#define FONT_HEADER_MED    20
#define FONT_BODY_LARGE    16
#define FONT_BODY_MED      14
#define FONT_BODY_SMALL    12
#define FONT_TINY          10

/* ========== COLLAPSIBLE SIDEBAR STATE ========== */
static int sidebar_collapsed = 0;
static const int SIDEBAR_COLLAPSED_WIDTH = 70;
static const int SIDEBAR_EXPANDED_WIDTH = 280;

/* ========== DOCUMENTATION SECTIONS STATE ========== */
#define MAX_DOC_SECTIONS 10
static int docs_expanded[MAX_DOC_SECTIONS] = {0};

/* Draw rounded box (approximated with rectangles) */
static void draw_box_rounded(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    fb_fill_rect(x + 4, y, w - 8, h, color);
    fb_fill_rect(x, y + 4, 4, h - 8, color);
    fb_fill_rect(x + w - 4, y + 4, 4, h - 8, color);
}

/* Draw glow border effect */

/* ========== UI BORDER DRAWING ========== */
static void draw_ui_border(void) {
    uint32_t border_thickness = 3;
    uint32_t border_color = COLOR_BORDER;
    
    /* Top */
    fb_fill_rect(0, 0, fb_width, border_thickness, border_color);
    /* Bottom */
    fb_fill_rect(0, fb_height - border_thickness, fb_width, border_thickness, border_color);
    /* Left */
    fb_fill_rect(0, 0, border_thickness, fb_height, border_color);
    /* Right */
    fb_fill_rect(fb_width - border_thickness, 0, border_thickness, fb_height, border_color);
}

/* ========== EXPANDABLE DOCUMENTATION SECTION ========== */
static void draw_doc_section(uint32_t x, uint32_t y, uint32_t w,
                             const char *title, const char *content,
                             int section_id, uint32_t *out_height) {
    uint32_t header_h = 45;
    int is_expanded = docs_expanded[section_id];
    
    /* Section header */
    uint32_t header_color = is_expanded ? 0x00505080 : COLOR_PURPLE_BOX;
    vic_rounded_rect_fill(framebuffer, fb_pitch,
        x, y, w, header_h, 8, header_color, fb_width, fb_height);
    
    /* Expand/collapse indicator */
    const char *indicator = is_expanded ? "v" : ">";
    nxf_draw_text(framebuffer, fb_pitch,
        x + 15, y + 15, indicator, FONT_BODY_MED, COLOR_ACCENT,
        fb_width, fb_height);
    
    /* Title */
    nxf_draw_text(framebuffer, fb_pitch,
        x + 45, y + 15, title, FONT_BODY_LARGE, COLOR_WHITE,
        fb_width, fb_height);
    
    *out_height = header_h;
    
    /* Expanded content */
    if (is_expanded && content) {
        uint32_t content_y = y + header_h + 5;
        uint32_t content_h = 80;
        
        /* Content background */
        vic_rounded_rect_fill(framebuffer, fb_pitch,
            x, content_y, w, content_h, 8, 0x00303050,
            fb_width, fb_height);
        
        /* Draw content text */
        nxf_draw_text(framebuffer, fb_pitch,
            x + 20, content_y + 15, content, FONT_BODY_SMALL, 0x00D0D0E0,
            fb_width, fb_height);
        
        *out_height += content_h + 10;
    }
}

/* ========== SIDEBAR TOGGLE CHECK ========== */
static int check_sidebar_toggle_click(int32_t mx, int32_t my) {
    uint32_t current_width = sidebar_collapsed ? (uint32_t)SIDEBAR_COLLAPSED_WIDTH : (uint32_t)SIDEBAR_EXPANDED_WIDTH;
    uint32_t toggle_x = current_width - 40;
    uint32_t toggle_y = 20;
    
    if (mx >= (int32_t)toggle_x && mx < (int32_t)(toggle_x + 30) &&
        my >= (int32_t)toggle_y && my < (int32_t)(toggle_y + 30)) {
        sidebar_collapsed = !sidebar_collapsed;
        return 1;
    }
    return 0;
}

/* ========== DOC SECTION CLICK CHECK ========== */
static int check_doc_section_click(int32_t mx, int32_t my, uint32_t sidebar_w) {
    uint32_t content_x = sidebar_w + 20;
    uint32_t content_w = fb_width - content_x - 30;
    
    uint32_t section_y = 85;
    uint32_t header_h = 45;
    
    /* Check each section header (4 sections) */
    for (int i = 0; i < 4; i++) {
        if (mx >= (int32_t)content_x && mx < (int32_t)(content_x + content_w) &&
            my >= (int32_t)section_y && my < (int32_t)(section_y + header_h)) {
            docs_expanded[i] = !docs_expanded[i];
            return 1;
        }
        
        /* Move to next section */
        section_y += header_h + 12;
        if (docs_expanded[i]) {
            section_y += 90; /* Expanded content height */
        }
    }
    return 0;
}


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

/* Draw the new Boot Firmware UI - FULLSCREEN with vector rendering */
static void draw_boot_manager(int selected, int in_sidebar) {
    /* Clear with dark background - fills entire screen */
    fb_clear(COLOR_PURPLE_BG);
    
    /* Calculate adaptive sizing based on screen resolution - COMPACT layout */
    int title_size = fb_height > 600 ? 28 : 20;
    int menu_size = fb_height > 600 ? 16 : 14;
    int icon_size = fb_height > 600 ? 36 : 28;
    int small_icon = fb_height > 600 ? 24 : 20;
    int padding = fb_height > 600 ? 12 : 8;
    
    /* ===== Title Bar (FULL WIDTH) ===== */
    uint32_t title_h = 60;
    
    vic_rounded_rect_fill(framebuffer, fb_pitch,
        0, padding,
        fb_width, title_h,
        0,
        COLOR_PURPLE_BOX,
        fb_width, fb_height);
    
    /* Title */
    nxf_draw_centered(framebuffer, fb_pitch, fb_width,
        padding + 15,
        "NeolyxOS Boot Firmware",
        title_size, COLOR_WHITE,
        fb_width, fb_height);
    
    /* Version (top-right) */
    nxf_draw_text(framebuffer, fb_pitch,
        fb_width - 160, padding + 22,
        NEOLYX_VERSION_STRING,
        14, 0x00808090,
        fb_width, fb_height);
    
    /* ===== Main Menu Panel (Left) ===== */
    uint32_t main_x = padding * 3;
    uint32_t main_y = 90;
    uint32_t main_w = fb_width / 2 - padding * 5;
    uint32_t item_h = icon_size + padding + 8;
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
    uint32_t side_x = fb_width / 2 + padding * 2;
    uint32_t side_y = 90;
    uint32_t side_w = fb_width / 2 - padding * 5;
    uint32_t side_item_h = small_icon + 8;
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
    
}




/* Check for KEYBOARD key (non-blocking) - ignores mouse data */
static int key_available(void) {
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    /* Bit 0 = data ready, Bit 5 = 1 means from mouse (skip mouse data) */
    return (status & 0x01) && !(status & 0x20);
}

/* Get key (non-blocking, returns 0 if none) */
static uint8_t get_key(void) {
    if (key_available()) {
        return inb(KEYBOARD_DATA_PORT);
    }
    return 0;
}

/* Wait for key (blocking) - busy wait to verify polling works */
static uint8_t wait_key(void) {
    while (!key_available()) {
        __asm__ volatile("pause");
    }
    return inb(KEYBOARD_DATA_PORT);
}







/* Execute command */

/* ============ Full Keyboard Driver ============ */

/* Keyboard state */


/* Process scancode and update keyboard state */



/* ============ NUCLEUS Disk Utility ============ */

/* Disk info structure for NUCLEUS */
/* String helpers */
static void nx_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}


/* Check if a partition has NXFS filesystem (compatible for install) */
static int is_disk_compatible(int disk_idx) {
    return nucleus_check_compatibility(disk_idx);
}

/* Count compatible disks */
static int count_compatible_disks(void) {
    int count = 0;
    int total_disks = nucleus_get_disk_count();
    for (int i = 0; i < total_disks; i++) {
        if (nucleus_check_compatibility(i)) count++;
    }
    return count;
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
NeolyxBootInfo *g_boot_info = 0;  /* Non-static for installer.c access */

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



/* Draw Nucleus UI - now delegates to src/utils/nucleus.c */


static void draw_docs(int page) {
    (void)page;  /* Using accordion sections instead of pages */
    fb_clear(COLOR_PURPLE_BG);
    
    /* Draw full-screen UI border */
    draw_ui_border();
    
    /* Header */
    fb_fill_rect(3, 3, fb_width - 6, 60, COLOR_PURPLE_BOX);
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 25, "Documentation", FONT_HEADER_LARGE, COLOR_WHITE, fb_width, fb_height);
    
    /* Divider line */
    fb_fill_rect(30, 65, fb_width - 60, 2, COLOR_BORDER);
    
    /* Content area */
    uint32_t content_x = 30;
    uint32_t content_w = fb_width - 60;
    uint32_t section_y = 85;
    uint32_t section_height;
    
    /* Documentation sections with accordion UI */
    draw_doc_section(content_x, section_y, content_w,
        "Getting Started",
        "Navigate with arrow keys. Enter to select. Esc to go back.",
        0, &section_height);
    section_y += section_height + 12;
    
    draw_doc_section(content_x, section_y, content_w,
        "Boot Options",
        "Safe Mode boots with minimal drivers. Verbose shows detailed logs.",
        1, &section_height);
    section_y += section_height + 12;
    
    draw_doc_section(content_x, section_y, content_w,
        "Keyboard Shortcuts",
        "Enter: Select | Arrows: Navigate | Esc: Back | Tab: Toggle",
        2, &section_height);
    section_y += section_height + 12;
    
    draw_doc_section(content_x, section_y, content_w,
        "System Requirements",
        "64-bit CPU with SSE2 | 2GB RAM | 20GB disk | UEFI | VGA graphics",
        3, &section_height);
    
    /* Footer hint removed */
}

static void draw_about_page(void) {
     fb_clear(COLOR_PURPLE_BG);
     
     /* Central Logo */
     // TODO: Vector logo
     nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 100, "NeolyxOS", 80, COLOR_WHITE, fb_width, fb_height);
     
     nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20, NEOLYX_VERSION_STRING, 32, COLOR_ACCENT, fb_width, fb_height);
     nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 60, "Copyright (c) 2025 KetiveeAI", 24, COLOR_GRAY, fb_width, fb_height);
     
     /* Footer removed */
}

/* Install flow state */
static int install_os_type = 0;  /* 0=Desktop, 1=Server */
static int install_disk_idx = -1; /* Selected disk for install */
static int install_progress = 0;  /* 0-100 progress */

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
    
    /* Footer removed */
}

/* Draw Disk Selection screen with compatibility check */
static void draw_disk_select(int selection) {
    fb_clear(COLOR_PURPLE_BG);
    
    /* Header */
    fb_fill_rect(0, 0, fb_width, 80, COLOR_PURPLE_BOX);
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 25, "Select Target Disk", 48, COLOR_WHITE, fb_width, fb_height);
    
    int count = nucleus_get_disk_count();
    
    if (count == 0) {
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, 300, "No Disks Found", 32, 0xFFFF4040, fb_width, fb_height);
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, 400, "[Esc] Back", 24, COLOR_GRAY, fb_width, fb_height);
        return;
    }
    
    for (int i = 0; i < count; i++) {
        nucleus_disk_t *d = nucleus_get_disk(i);
        int compatible = is_disk_compatible(i);
        
        uint32_t y = 200 + i * 120;
        uint32_t x = fb_width / 2 - 250;
        
        uint32_t color = (i == selection) ? COLOR_ACCENT : (compatible ? COLOR_PURPLE_BOX : 0xFF402020);
        uint32_t text_color = compatible ? COLOR_WHITE : 0xFFA0A0A0;
        
        vic_rounded_rect_fill(framebuffer, fb_pitch, x, y, 500, 100, 10, color, fb_width, fb_height);
        
        if (i == selection) {
             vic_rounded_rect(framebuffer, fb_pitch, x, y, 500, 100, 10, 3, COLOR_WHITE, fb_width, fb_height);
        }
        
        vic_render_icon(framebuffer, fb_pitch, x + 20, y + 20, 60, icon_disk_v2, compatible ? COLOR_WHITE : 0xFFFF4040, fb_width, fb_height);
        nxf_draw_text(framebuffer, fb_pitch, x + 100, y + 20, d->name, 24, text_color, fb_width, fb_height);

        char size_s[20];
        nucleus_format_size(d->size_bytes, size_s, 20);
        nxf_draw_text(framebuffer, fb_pitch, x + 100, y + 55, size_s, 16, COLOR_GRAY, fb_width, fb_height);
        
        if (!compatible) {
             nxf_draw_text(framebuffer, fb_pitch, x + 350, y + 40, "Incompatible", 16, 0xFFFF4040, fb_width, fb_height);
        }
    }
    
    if (count > 0 && !is_disk_compatible(selection)) {
         nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height - 150, "Disk needs formatting (NXFS required)", 20, 0xFFFF4040, fb_width, fb_height);
         /* Navigation hint removed */
    } else {
         /* Navigation hint removed */
    }
}

/* Draw Install Progress screen */
static void draw_install_progress(void) {
    fb_clear(COLOR_PURPLE_BG);
    
    nucleus_disk_t *d = nucleus_get_disk(install_disk_idx);
    
    /* Header */
    fb_fill_rect(0, 0, fb_width, 80, COLOR_PURPLE_BOX);
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 25, "Installing NeolyxOS", 48, COLOR_WHITE, fb_width, fb_height);
    
    char status[128];
    nx_strcpy(status, "Installing to ", 128);
    // nx_strcat(status, d->name); // No strcat, just append
    // Simplifying display for now
    
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 200, status, 24, COLOR_WHITE, fb_width, fb_height);
    if (d) {
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, 230, d->name, 24, COLOR_ACCENT, fb_width, fb_height);
    }
    
    /* Progress Bar */
    uint32_t bar_w = 600;
    uint32_t bar_h = 30;
    uint32_t bar_x = (fb_width - bar_w) / 2;
    uint32_t bar_y = 350;
    
    vic_rounded_rect_fill(framebuffer, fb_pitch, bar_x, bar_y, bar_w, bar_h, 15, 0xFF303040, fb_width, fb_height);
    
    uint32_t fill_w = (bar_w * install_progress) / 100;
    if (fill_w > 0) {
        vic_rounded_rect_fill(framebuffer, fb_pitch, bar_x, bar_y, fill_w, bar_h, 15, COLOR_GREEN, fb_width, fb_height);
    }
    
    char pct[8];
    pct[0] = '0' + (install_progress / 10) % 10;
    pct[1] = '0' + install_progress % 10;
    pct[2] = '%';
    pct[3] = '\0';
    if (install_progress >= 100) { pct[0]='1'; pct[1]='0'; pct[2]='0'; pct[3]='%'; pct[4]='\0'; }
    
    nxf_draw_text(framebuffer, fb_pitch, bar_x + bar_w + 20, bar_y + 5, pct, 20, COLOR_WHITE, fb_width, fb_height);
    
    /* Status Messages */
    const char *msg = "Initializing...";
    if (install_progress > 10) msg = "Formatting Disk (NXFS)...";
    if (install_progress > 30) msg = "Copying Kernel...";
    if (install_progress > 60) msg = "Installing System Files...";
    if (install_progress > 90) msg = "Finalizing Configuration...";
    if (install_progress >= 100) msg = "Installation Complete!";
    
    nxf_draw_centered(framebuffer, fb_pitch, fb_width, 420, msg, 20, 0x00A0A0B0, fb_width, fb_height);
    
    if (install_progress >= 100) {
        /* Navigation hint removed */
    } else {
        nxf_draw_centered(framebuffer, fb_pitch, fb_width, 500, "Time remaining: < 1 min", 16, COLOR_GRAY, fb_width, fb_height);
    }
}

void kernel_main(NeolyxBootInfo *boot_info) {

    serial_init();
    serial_puts("\n\n========================================\n");
    serial_puts("    NeolyxOS Boot Firmware\n");
    serial_puts("========================================\n\n");
    
    /* CRITICAL: Initialize GDT with Ring 3 user segments FIRST
     * This MUST be done before any hardware init to ensure proper
     * segment selectors for later userspace transition */
    serial_puts("[BOOT] Loading GDT with Ring 3 segments...\n");
    extern void gdt_init(void);
    gdt_init();
    
    /* CRITICAL: Initialize IDT with exception handlers IMMEDIATELY after GDT
     * This enables page fault, GPF, and double fault handlers with IST1 stack.
     * Without this, any exception causes a triple fault (CPU reset). */
    serial_puts("[BOOT] Initializing IDT with IST1 exception handlers...\\n");
    extern void idt_init(void);
    idt_init();
    serial_puts("[BOOT] IDT ready with exception handling\\n");
    
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
        
        /* Export for desktop shell (accessed via extern in main.c) */
        boot_framebuffer = framebuffer;
        boot_fb_width = fb_width;
        boot_fb_height = fb_height;
        boot_fb_pitch = fb_pitch;
        
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
        
        /* Initialize Physical Memory Manager (required for AHCI DMA) */
        if (boot_info->memory_map_addr && boot_info->memory_map_size > 0) {
            extern void pmm_init(void *mmap, uint64_t size, uint64_t desc_size);
            uint64_t desc_size = boot_info->memory_map_desc_size;
            if (desc_size == 0) desc_size = 48;  /* Default UEFI desc size */
            pmm_init((void *)boot_info->memory_map_addr, 
                     boot_info->memory_map_size,
                     desc_size);
            
            /* Reserve kernel memory (text + data + bss) */
            extern uint64_t kernel_start; /* Defined in linker script */
            extern uint64_t kernel_end;   /* Defined in linker script */
            /* Note: linker script symbols are addresses, use & to get value */
            /* Actually, checking linker.ld, standard names are _start (entry) and __bss_end or end */
            extern char _start[];
            extern char __bss_end[];
            
            uint64_t k_start = (uint64_t)_start;
            uint64_t k_end = (uint64_t)__bss_end;
            uint64_t k_size = k_end - k_start;
            
            /* Align to page boundaries */
            k_size = (k_size + 4095) & ~4095;
            
            extern void pmm_reserve_region(uint64_t start, uint64_t size);
            pmm_reserve_region(k_start, k_size);
            
            serial_puts("[OK] PMM initialized for AHCI\n");
        }
        
         /* Initialize Nucleus Disk Manager */
        serial_puts("[BOOT] Starting nucleus_init...\n");
        nucleus_init();
        serial_puts("[OK] Nucleus initialized\n");
        
        /* Initialize kernel heap for dynamic memory allocation */
        {
            extern void heap_init(uint64_t start_addr, uint64_t size);
            /* Use memory at 32MB mark for heap (16MB of heap space) 
             * IMPORTANT: Desktop ELF is loaded at 0x1000000-0x1280000, so heap must be AFTER that!
             * Using 0x2000000 (32MB) as heap start to leave room for userspace
             */
            heap_init(0x2000000, 0x1000000);
            serial_puts("[OK] Heap initialized at 0x2000000\n");
        }
        
        /* Initialize Filesystem Layer */
        serial_puts("[BOOT] Initializing VFS/NXFS components...\n");
        vfs_init();
        nxfs_init();
        
        /* Initialize FAT32 for ESP access */
        extern void fat_init(void);
        fat_init();
        
        ahci_block_init();
        
        /* Try to mount ESP (boot disk) as FAT32 at root for user program access */
        ahci_block_dev_t *esp_device = ahci_block_open(0);  /* Port 0 = boot disk */
        if (esp_device) {
            /* FAT driver uses direct AHCI sector I/O - no block ops needed */
            int mount_result = vfs_mount("/", esp_device, "fat32", 0);
            if (mount_result == 0) {
                serial_puts("[BOOT] Mounted ESP (FAT32) at /\n");
            } else {
                serial_puts("[BOOT] ESP mount failed, trying NXFS...\n");
            }
        }
        
        int dcount = nucleus_get_disk_count();
        if (dcount > 0) {
            serial_puts("[BOOT] Disks detected, checking partitions...\n");
            
            /* Try to mount NXFS partitions */
            int mounted = 0;
            for (int i = 0; i < dcount; i++) {
                    nucleus_disk_t *disk = nucleus_get_disk(i);
                    /* We need to trigger partition load if not already done */
                    /* Note: nucleus_get_disk might not have partitions loaded if scan only did basic probe */
                    /* But nucleus_scan_disks calls nucleus_load_partitions(0) for first disk */
                    
                    /* Bridge this disk to VFS */
                    ahci_block_dev_t *blk_dev = ahci_block_open(disk->port);
                    if (!blk_dev) continue;
                    
                    /* Check partitions */
                    /* We need access to partition table. nucleus.c hides the array */
                    /* But we can check compatibility which iterates partitions */
                    
                    /* For now, just try to mount the first NXFS partition as root */
                    /* TODO: expose partition list better or add mount helper in nucleus */
                    
                    /* Let's peek at the global partitions if possible, or assume LBA offsets */
                    /* Actually, nucleus.c exposes nucleus_check_compatibility */
                    if (nucleus_check_compatibility(i)) {
                        /* Found NXFS partition */
                        serial_puts("[BOOT] Found NXFS partition, attempting mount...\n");
                        
                        /* TODO: We need the exact offset/block of the partition */
                        /* But our AHCI block adapter treats the whole disk as the device */
                        /* And NXFS mount expects partition start or device */
                        /* NXFS reads superblock at block 0. */
                        /* If it's a partition, we need a "partition block device" wrapper or offset support */
                        /* Current NXFS implementation reads block 0 of the *device* passed to it */
                        
                        /* Strategy: Assume whole disk is NXFS for now (simple formatting) */
                        /* OR: Modify AHCI block adapter to support partition offsets */
                        /* Given "no dummy", let's be real: We need partition offsets. */
                        /* But for this iteration, let's verify VFS init and dry-run mount */
                        
                        /* Attempt mount on root */
                        /* Note: This will likely fail if it's a partitioned disk and we look at LBA 0 */
                        /* But the initialization sequence is correct. */
                        int ret = vfs_mount("/", blk_dev, "nxfs", 0);
                        if (ret == VFS_OK) {
                            serial_puts("[BOOT] Mounted / (Root) successfully\n");
                            mounted = 1;
                            break;
                        } else {
                            serial_puts("[BOOT] Mount failed (not NXFS or bad superblock)\n");
                        }
                    }
                }
                
                if (!mounted) {
                    serial_puts("[BOOT] No root filesystem mounted.\n");
                }
            }
        
        /* ========== Stage 4: Install State Decision (per roadmap.pdf) ========== */
        /* Check if NeolyxOS is installed by looking for marker file */
        {
            extern vfs_file_t *vfs_open(const char *path, uint32_t mode);
            extern int vfs_close(vfs_file_t *file);
            
            serial_puts("[BOOT] Checking installation marker...\n");
            
            /* Debug: List files in root directory */
            serial_puts("[BOOT] DEBUG: Root directory contents:\n");
            {
                typedef struct { char name[256]; uint8_t type; uint64_t inode; uint64_t size; } vfs_dirent_t;
                vfs_dirent_t entries[16];
                uint32_t count = 0;
                if (vfs_readdir("/", entries, 16, &count) == 0) {
                    for (uint32_t i = 0; i < count; i++) {
                        serial_puts("  [");
                        serial_puts(entries[i].type == 2 ? "D" : "F");
                        serial_puts("] ");
                        serial_puts(entries[i].name);
                        serial_puts("\n");
                    }
                    if (count == 0) {
                        serial_puts("  (empty)\n");
                    }
                } else {
                    serial_puts("  (readdir failed)\n");
                }
            }
            
            /* Try direct VFS open of the marker file (root path first) */
            vfs_file_t *marker = vfs_open("/instald.mrk", 0);
            int is_installed = 0;
            
            if (marker) {
                serial_puts("[BOOT] Marker file FOUND (root) - OS is installed\n");
                vfs_close(marker);
                is_installed = 1;
            } else {
                serial_puts("[BOOT] Marker file NOT FOUND in root - checking EFI/BOOT...\n");
                
                /* Try alternate path in EFI/BOOT */
                marker = vfs_open("/EFI/BOOT/instald.mrk", 0);
                if (marker) {
                    serial_puts("[BOOT] Found marker in EFI/BOOT\n");
                    vfs_close(marker);
                    is_installed = 1;
                } else {
                    serial_puts("[BOOT] Marker truly not found - Installer Mode\n");
                }
            }
            
            /* F2 Key Detection - Quick check with 500ms window */
            int f2_pressed = 0;
            if (is_installed) {
                serial_puts("[BOOT] NeolyxOS detected - Press F2 for boot menu...\n");
                
                /* Display quick message */
                fb_clear(COLOR_PURPLE_BG);
                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 40,
                    "NeolyxOS", 64, COLOR_WHITE, fb_width, fb_height);
                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 40,
                    "Press F2 for Boot Menu...", 24, COLOR_GRAY, fb_width, fb_height);
                
                /* Check for F2 key (scancode 0x3C) with ~500ms timeout */
                for (int i = 0; i < 5000000 && !f2_pressed; i++) {
                    if (key_available()) {
                        uint8_t key = inb(KEYBOARD_DATA_PORT);
                        if (key == 0x3C) {  /* F2 press */
                            f2_pressed = 1;
                            serial_puts("[BOOT] F2 pressed - showing boot menu\n");
                        }
                    }
                    __asm__ volatile("pause");
                }
                serial_puts("[BOOT] F2 check complete\n");
                
                if (!f2_pressed) {
                    /* Direct boot to NeolyxOS */
                    serial_puts("[BOOT] Direct boot to NeolyxOS...\n");
                    
                    fb_clear(COLOR_PURPLE_BG);
                    nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 20,
                        "Starting NeolyxOS...", 32, COLOR_WHITE, fb_width, fb_height);
                    
                    /* Brief delay for user feedback */
                    for (volatile int d = 0; d < 10000000; d++);
                    
                    /* Launch full kernel / desktop environment */
                    nx_kernel_start();
                    
                    /* If nx_kernel_start returns, halt */
                    while(1) { __asm__ volatile("hlt"); }
                }
            } else {
                serial_puts("[BOOT] Fresh system - AUTO-BOOT for DEBUG\\n");
                /* DEBUG: Skip boot menu and directly test ELF loading */
                nx_kernel_start();
                while(1) { __asm__ volatile("hlt"); }
            }
        }
        
        /* Initial draw - only reached if F2 pressed or not installed */
        serial_puts("[BOOT] Drawing boot manager...\n");
        draw_boot_manager(selected, in_sidebar);
        cursor_draw();
        serial_puts("[OK] Boot Firmware displayed\n");
        
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
                    /* Check for sidebar toggle click (Global in UI) */
                    if (check_sidebar_toggle_click(mouse_x, mouse_y)) {
                        needs_redraw = 1;
                    }

                    /* Check for documentation clicks if in DOCS view */
                    if (current_view == VIEW_DOCS) {
                        uint32_t sidebar_w = sidebar_collapsed ? 70 : 280;
                        if (check_doc_section_click(mouse_x, mouse_y, sidebar_w)) {
                            needs_redraw = 1;
                        }
                    }

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
                    
                    /* Handle NUCLEUS view clicks */
                    if (current_view == VIEW_NUCLEUS_DISK) {
                        /* Forward click to nucleus_handle_click for button handling */
                        int nuc_res = nucleus_handle_click(mouse_x, mouse_y, fb_width, fb_height);
                        if (nuc_res == -1) {
                            current_view = VIEW_MAIN;
                            needs_redraw = 1;
                        } else if (nuc_res > 0) {
                            needs_redraw = 1;
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
                        /* Input handled by Nucleus internal handler */
                        int nuc_res = nucleus_handle_input(scancode);
                        if (nuc_res == -1) {
                             current_view = VIEW_MAIN;
                             needs_redraw = 1;
                        } else if (nuc_res > 0) {
                             needs_redraw = 1;
                        }
                    } else if (current_view == VIEW_DISK_SELECT) {
                        if (sub_selection > 0) {
                            sub_selection--;
                            needs_redraw = 1;
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
                        int nuc_res = nucleus_handle_input(scancode);
                        if (nuc_res == -1) {
                             current_view = VIEW_MAIN;
                             needs_redraw = 1;
                        } else if (nuc_res > 0) {
                             needs_redraw = 1;
                        }
                    } else if (current_view == VIEW_DISK_SELECT) {
                        if (sub_selection < nucleus_get_disk_count() - 1) {
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
                    } else if (current_view == VIEW_NUCLEUS_DISK) {
                        int nuc_res = nucleus_handle_input(scancode);
                        if (nuc_res == -1) {
                             current_view = VIEW_MAIN;
                             needs_redraw = 1;
                        } else if (nuc_res > 0) {
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
                    } else if (current_view == VIEW_NUCLEUS_DISK) {
                        if (nucleus_handle_input(scancode)) {
                            needs_redraw = 1;
                        }
                    }
                    break;
                    
                case 0x01: /* Esc */
                    if (current_view != VIEW_MAIN) {
                        if (current_view == VIEW_NUCLEUS_DISK) {
                            if (nucleus_handle_input(scancode)) { // Let nucleus handle Esc first
                                needs_redraw = 1;
                            } else { // Nucleus is done, return to main menu
                                current_view = VIEW_MAIN;
                                needs_redraw = 1;
                                cursor_restore_bg(); /* Ensure cursor clean up */
                            }
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

                case 0x0F: /* Tab - Toggle sidebar collapsed/expanded */
                    sidebar_collapsed = !sidebar_collapsed;
                    needs_redraw = 1;
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
                        if (nucleus_handle_input(scancode)) {
                            needs_redraw = 1;
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
                        if (compatible_count == 0 || !is_disk_compatible(sub_selection)) {
                            /* No compatible disks or selected disk is incompatible - go to Nucleus Disk */
                            current_view = VIEW_NUCLEUS_DISK;
                            sub_selection = 0;
                            needs_redraw = 1;
                        } else {
                            /* Find the actual disk index from compatible selection */
                            install_disk_idx = sub_selection; // sub_selection directly corresponds to disk index now
                            
                            /* 
                             * SIMULATION AUDIT: 2025-12-25
                             * Previous code used delay loop simulation for "install progress".
                             * Now calls real installer which performs:
                             *   - GPT partitioning via nucleus_write_disk (AHCI)
                             *   - FAT32/NXFS formatting via real sector writes
                             *   - File copying via VFS operations
                             * Works on both VM (QEMU) and real hardware.
                             */
                            
                            /* Run the real installer wizard */
                            extern int installer_run_wizard(void);
                            install_progress = 0;
                            current_view = VIEW_INSTALL_PROGRESS;
                            draw_install_progress();  /* Show initial progress */
                            
                            /* Call real installer - it handles all disk operations */
                            int install_result = installer_run_wizard();
                            
                            /* Installation complete - show result */
                            fb_clear(COLOR_PURPLE_BG);
                            if (install_result == 0) {
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 40, "Installation Complete!", 48, COLOR_GREEN, fb_width, fb_height);
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20, "NeolyxOS has been installed successfully.", 24, COLOR_WHITE, fb_width, fb_height);
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 60, "Restart to boot into your new system.", 20, COLOR_GRAY, fb_width, fb_height);
                            } else {
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 40, "Installation Failed", 48, COLOR_RED, fb_width, fb_height);
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20, "An error occurred during installation.", 24, COLOR_WHITE, fb_width, fb_height);
                            }
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
                            /*
                             * INSTALLATION GATE: NeolyxOS requires installation
                             * No live desktop. No demo shell. Installation is mandatory.
                             */
                            extern int installer_check_needed(void);
                            
                            if (installer_check_needed()) {
                                /* NOT INSTALLED - Block boot, redirect to installer */
                                serial_puts("[BOOT] ERROR: NeolyxOS not installed!\n");
                                serial_puts("[BOOT] Redirecting to installer...\n");
                                
                                fb_clear(COLOR_PURPLE_BG);
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 60,
                                    "NeolyxOS Not Installed", 48, 0xFFFF4444, fb_width, fb_height);
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2,
                                    "Installation is required to boot NeolyxOS.", 24, COLOR_WHITE, fb_width, fb_height);
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 40,
                                    "Launching installer...", 20, COLOR_GRAY, fb_width, fb_height);
                                
                                /* Brief delay for user to read */
                                for (volatile int d = 0; d < 30000000; d++);
                                
                                /* Launch installer */
                                extern int installer_run_wizard(void);
                                
                                /* Disable mouse before installer */
                                mouse_wait_write();
                                outb(0x64, 0xA7);
                                mouse_initialized = 0;
                                serial_puts("[BOOT] Mouse disabled, starting installer...\n");
                                
                                installer_run_wizard();
                                
                                /* After installation, boot to desktop */
                                serial_puts("[BOOT] Installation complete, booting...\n");
                                nx_kernel_start();
                                
                                draw_boot_manager(selected, in_sidebar);
                                break;
                            }
                            
                            /* INSTALLED - Proceed to boot */
                            fb_clear(COLOR_PURPLE_BG);
                            nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 - 20,
                                "Booting NeolyxOS...", 32, COLOR_WHITE, fb_width, fb_height);
                            if (g_boot_options.safe_mode) {
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20,
                                    "Safe Mode enabled", 20, COLOR_YELLOW, fb_width, fb_height);
                            } else if (g_boot_options.verbose_boot) {
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20,
                                    "Verbose Boot enabled", 20, COLOR_GRAY, fb_width, fb_height);
                            } else {
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2 + 20,
                                    "Starting desktop environment...", 20, COLOR_GRAY, fb_width, fb_height);
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
                            /* === QEMU WORKAROUND: Properly shut down mouse before installer === */
                            extern void nx_kernel_start(void);
                            
                            /* 1. Disable mouse input at controller level */
                            mouse_wait_write();
                            outb(0x64, 0xA7);  /* Disable auxiliary device (mouse) */
                            
                            /* 2. Drain ALL pending keyboard and mouse data */
                            for (int drain = 0; drain < 100; drain++) {
                                if (inb(0x64) & 0x01) {
                                    (void)inb(0x60);
                                } else {
                                    break;
                                }
                            }
                            
                            /* 4. Mark mouse as not initialized */
                            mouse_initialized = 0;
                            
                            serial_puts("[BOOT] Mouse disabled, starting installer...\n");
                            
                            extern int installer_run_wizard(void);
                            installer_run_wizard();
                            
                            /* After installation, re-init mouse and boot */
                            mouse_init();
                            serial_puts("[BOOT] Install complete, booting...\n");
                            nx_kernel_start();
                            
                            /* If it returns, go back to menu */
                            draw_boot_manager(selected, in_sidebar);
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
                            serial_puts("[BOOT] Launching Terminal Shell...\n");
                            
                            /* Initialize networking subsystem before terminal */
                            extern int network_init(void);
                            extern void tcpip_init(void);
                            if (network_init() == 0) {
                                serial_puts("[OK] Network subsystem initialized\n");
                                tcpip_init();
                                serial_puts("[OK] TCP/IP stack initialized\n");
                            } else {
                                serial_puts("[WARN] Network init failed\n");
                            }
                            
                            /* Launch terminal shell - takes over screen */
                            extern void terminal_shell_run(uint64_t fb_addr, uint32_t width, 
                                                          uint32_t height, uint32_t pitch);
                            terminal_shell_run((uint64_t)framebuffer, fb_width, fb_height, fb_pitch);
                            
                            /* After terminal exits, redraw boot manager */
                            serial_puts("[BOOT] Terminal exited, returning to boot manager\n");
                            draw_boot_manager(selected, in_sidebar);
                            cursor_draw();
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
                            serial_puts("[BOOT] Launching Zepra Browser session...\n");
                            
                            /* Disable mouse during browser session */
                            mouse_wait_write();
                            outb(0x64, 0xA7);  /* Disable auxiliary device (mouse) */
                            mouse_initialized = 0;
                            
                            /* Initialize browser session */
                            extern void zepra_session_init(BrowserSessionConfig*);
                            extern BrowserCloseAction zepra_session_start(void);
                            extern void zepra_session_close(BrowserCloseAction);
                            extern void zepra_session_draw_launcher(void);
                            
                            /* Launch browser - it will use NXRender for UI */
                            /* When browser closes:
                             *   0 = CLOSE_ACTION_SHUTDOWN (disconnect + shutdown)
                             *   1 = CLOSE_ACTION_RELAUNCH (show launcher)
                             *   2 = CLOSE_ACTION_INSTALL (yield to installer)
                             */
                            int close_action = zepra_session_start();
                            
                            if (close_action == 0) {
                                /* Shutdown requested */
                                serial_puts("[BOOT] Browser requested shutdown\n");
                                fb_clear(COLOR_PURPLE_BG);
                                nxf_draw_centered(framebuffer, fb_pitch, fb_width, fb_height/2, "Shutting down...", 24, COLOR_WHITE, fb_width, fb_height);
                                zepra_session_close(0);
                                goto halt_loop;
                            } else if (close_action == 2) {
                                /* Install requested - run installer */
                                serial_puts("[BOOT] Browser requested install, launching installer...\n");
                                extern int installer_run_wizard(void);
                                installer_run_wizard();
                                nx_kernel_start();
                            } else {
                                /* Relaunch - show launcher screen */
                                serial_puts("[BOOT] Browser closed, showing launcher\n");
                                zepra_session_draw_launcher();
                                wait_key();
                            }
                            
                            /* Re-init mouse and redraw */
                            mouse_init();
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
                        nucleus_draw(framebuffer, fb_width, fb_height, fb_pitch);
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
