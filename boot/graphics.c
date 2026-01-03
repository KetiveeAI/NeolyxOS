/*
 * NeolyxOS Bootloader Graphics - GOP Framebuffer Drawing
 * 
 * UEFI Graphics Output Protocol implementation for GUI-first boot
 * 
 * Copyright (c) 2025 Ketivee Organization - KetiveeAI License
 */

#include <efi.h>
#include <efilib.h>
#include "graphics.h"
#include "logo.h"

/* Global GOP pointer */
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gGOP = NULL;
static UINT32 *gFramebuffer = NULL;
static UINT32 gWidth = 0;
static UINT32 gHeight = 0;
static UINT32 gPixelsPerScanLine = 0;

/* Use gnu-efi's built-in GOP GUID */
#define GraphicsOutputProtocol gEfiGraphicsOutputProtocolGuid


/* Initialize Graphics */
EFI_STATUS graphics_init(EFI_SYSTEM_TABLE *ST) {
    EFI_STATUS status;
    
    /* Locate GOP */
    status = uefi_call_wrapper(
        ST->BootServices->LocateProtocol,
        3,
        &GraphicsOutputProtocol,
        NULL,
        (VOID**)&gGOP
    );
    
    if (EFI_ERROR(status) || gGOP == NULL) {
        Print(L"ERROR: Graphics Output Protocol not found!\n");
        return status;
    }
    
    /* Get current mode info */
    gWidth = gGOP->Mode->Info->HorizontalResolution;
    gHeight = gGOP->Mode->Info->VerticalResolution;
    gPixelsPerScanLine = gGOP->Mode->Info->PixelsPerScanLine;
    gFramebuffer = (UINT32*)(UINTN)gGOP->Mode->FrameBufferBase;
    
    return EFI_SUCCESS;
}

/* Get screen dimensions */
UINT32 graphics_get_width(void) { return gWidth; }
UINT32 graphics_get_height(void) { return gHeight; }

/* Set a single pixel (BGRA format) */
void graphics_put_pixel(UINT32 x, UINT32 y, UINT32 color) {
    if (x < gWidth && y < gHeight && gFramebuffer) {
        gFramebuffer[y * gPixelsPerScanLine + x] = color;
    }
}

/* Clear screen with color */
void graphics_clear(UINT32 color) {
    if (!gFramebuffer) return;
    
    for (UINT32 y = 0; y < gHeight; y++) {
        for (UINT32 x = 0; x < gWidth; x++) {
            gFramebuffer[y * gPixelsPerScanLine + x] = color;
        }
    }
}

/* Draw filled rectangle */
void graphics_fill_rect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT32 color) {
    for (UINT32 j = 0; j < h; j++) {
        for (UINT32 i = 0; i < w; i++) {
            graphics_put_pixel(x + i, y + j, color);
        }
    }
}

/* Draw rectangle outline */
void graphics_draw_rect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT32 color, UINT32 thickness) {
    /* Top */
    graphics_fill_rect(x, y, w, thickness, color);
    /* Bottom */
    graphics_fill_rect(x, y + h - thickness, w, thickness, color);
    /* Left */
    graphics_fill_rect(x, y, thickness, h, color);
    /* Right */
    graphics_fill_rect(x + w - thickness, y, thickness, h, color);
}

/* Draw progress bar */
void graphics_draw_progress_bar(UINT32 x, UINT32 y, UINT32 w, UINT32 h, 
                                 UINT32 progress, UINT32 bg_color, UINT32 fg_color) {
    /* Background */
    graphics_fill_rect(x, y, w, h, bg_color);
    
    /* Progress fill */
    UINT32 fill_width = (w * progress) / 100;
    if (fill_width > 0) {
        graphics_fill_rect(x, y, fill_width, h, fg_color);
    }
    
    /* Border */
    graphics_draw_rect(x, y, w, h, COLOR_WHITE, 2);
}

/* Draw NeolyxOS Logo (centered) */
void graphics_draw_logo(void) {
    if (!gFramebuffer) return;
    
    /* Calculate centered position */
    UINT32 logo_x = (gWidth - LOGO_WIDTH) / 2;
    UINT32 logo_y = (gHeight / 2) - LOGO_HEIGHT - 50;
    
    /* Draw logo from embedded data */
    for (UINT32 y = 0; y < LOGO_HEIGHT; y++) {
        for (UINT32 x = 0; x < LOGO_WIDTH; x++) {
            UINT32 color = neolyx_logo[y * LOGO_WIDTH + x];
            if (color != 0) {  /* Skip transparent pixels */
                graphics_put_pixel(logo_x + x, logo_y + y, color);
            }
        }
    }
}

/* Draw simple text at position (basic 8x8 font) */
static const UINT8 font_8x8[96][8] = {
    /* Space to ~ (ASCII 32-126) - simplified */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* ! */
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
    /* ... simplified - we'll mainly use H, e, l, o, N, etc. */
    [0] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

void graphics_draw_char(UINT32 x, UINT32 y, CHAR16 c, UINT32 color, UINT32 scale) {
    /* Simple character drawing - just draw a block for now */
    graphics_fill_rect(x, y, 8 * scale, 8 * scale, color);
}

void graphics_draw_text(UINT32 x, UINT32 y, CHAR16 *text, UINT32 color, UINT32 scale) {
    while (*text) {
        graphics_draw_char(x, y, *text, color, scale);
        x += 10 * scale;
        text++;
    }
}

/* Show splash screen with logo and progress */
void show_graphical_splash(EFI_SYSTEM_TABLE *ST, CHAR16 *message, UINT32 progress) {
    if (!gFramebuffer) {
        /* Fallback to text mode */
        Print(message);
        return;
    }
    
    /* Clear to dark background */
    graphics_clear(COLOR_DARK_BG);
    
    /* Draw logo */
    graphics_draw_logo();
    
    /* Draw progress bar (centered, below logo) */
    UINT32 bar_w = 400;
    UINT32 bar_h = 20;
    UINT32 bar_x = (gWidth - bar_w) / 2;
    UINT32 bar_y = (gHeight / 2) + 100;
    
    graphics_draw_progress_bar(bar_x, bar_y, bar_w, bar_h, 
                               progress, COLOR_DARK_GRAY, COLOR_CYAN);
    
    /* Draw "Hello from NeolyxOS" text area */
    UINT32 text_y = bar_y + 50;
    graphics_fill_rect((gWidth - 300) / 2, text_y, 300, 30, COLOR_DARK_BG);
}

/* Animate progress bar */
void graphics_animate_progress(EFI_SYSTEM_TABLE *ST, UINT32 start, UINT32 end, UINT32 delay_ms) {
    for (UINT32 p = start; p <= end; p += 5) {
        show_graphical_splash(ST, L"Loading...", p);
        uefi_call_wrapper(ST->BootServices->Stall, 1, delay_ms * 1000);
    }
}