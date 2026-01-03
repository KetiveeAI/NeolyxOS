/*
 * NeolyxOS Browser Session Manager Implementation
 * 
 * Uses REOX-NXRender for all UI rendering (no SDL)
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "browser_session.h"
#include <stddef.h>

/* External kernel functions */
extern void serial_puts(const char *s);
extern void outb(uint16_t port, uint8_t val);
extern void outw(uint16_t port, uint16_t val);
extern volatile uint32_t *framebuffer;
extern uint32_t fb_width, fb_height, fb_pitch;

/* Session state */
static BrowserSessionState g_session_state = SESSION_IDLE;
static BrowserSessionConfig g_session_config;

/* NXRender state for launcher UI */
static bool g_nxrender_initialized = false;

/* ============================================================================
 * Session Lifecycle Implementation
 * ============================================================================ */

void zepra_session_init(BrowserSessionConfig* config) {
    if (!config) return;
    
    g_session_config = *config;
    g_session_state = SESSION_IDLE;
    
    serial_puts("[SESSION] Browser session manager initialized\n");
}

BrowserCloseAction zepra_session_start(void) {
    serial_puts("[SESSION] Starting ZepraBrowser session...\n");
    g_session_state = SESSION_RUNNING;
    
    /* Initialize NXRender for browser UI */
    zepra_nxrender_init(g_session_config.fb_width, g_session_config.fb_height);
    
    /* 
     * NOTE: In full implementation, this would launch the actual browser
     * using NXRender for all UI rendering.
     * 
     * For now, we return SHUTDOWN action as placeholder.
     * The ZepraBrowser application (zepra_browser.cpp) handles the main loop.
     */
    
    /* When browser exits, it should call zepra_session_close() with appropriate action */
    
    g_session_state = SESSION_IDLE;
    serial_puts("[SESSION] ZepraBrowser session ended\n");
    
    return CLOSE_ACTION_RELAUNCH;  /* Default: show launcher */
}

void zepra_session_close(BrowserCloseAction action) {
    serial_puts("[SESSION] Closing browser session...\n");
    
    switch (action) {
        case CLOSE_ACTION_SHUTDOWN:
            zepra_session_shutdown();
            break;
            
        case CLOSE_ACTION_INSTALL:
            g_session_state = SESSION_INSTALL;
            zepra_session_request_install();
            break;
            
        case CLOSE_ACTION_RELAUNCH:
            g_session_state = SESSION_RELAUNCH;
            zepra_session_draw_launcher();
            break;
    }
}

void zepra_session_request_install(void) {
    serial_puts("[SESSION] Browser yielding to installer...\n");
    
    /* Shutdown NXRender gracefully */
    zepra_nxrender_shutdown();
    
    /* Installer will be called by boot_firmware.c */
    g_session_state = SESSION_INSTALL;
}

void zepra_session_shutdown(void) {
    serial_puts("[SESSION] Initiating system shutdown...\n");
    
    /* 1. Disconnect network */
    extern int network_shutdown(void);
    network_shutdown();
    serial_puts("[SESSION] Network disconnected\n");
    
    /* 2. Sync filesystems */
    extern void vfs_sync_all(void);
    vfs_sync_all();
    serial_puts("[SESSION] Filesystems synced\n");
    
    /* 3. Shutdown NXRender */
    zepra_nxrender_shutdown();
    
    /* 4. ACPI S5 power off (QEMU compatible) */
    outw(0x604, 0x2000);
    
    /* 5. Fallback: keyboard controller reset */
    for (volatile int i = 0; i < 1000000; i++);
    outb(0x64, 0xFE);
    
    /* Should not reach here */
    g_session_state = SESSION_IDLE;
}

BrowserSessionState zepra_session_state(void) {
    return g_session_state;
}

/* ============================================================================
 * Launcher UI (Click to re-open browser)
 * ============================================================================ */

/* Simple launcher icon drawing using framebuffer primitives */
static void draw_launcher_icon(uint32_t cx, uint32_t cy, uint32_t size, uint32_t color) {
    /* Draw a simple browser icon (rounded rect with circle) */
    uint32_t x = cx - size/2;
    uint32_t y = cy - size/2;
    
    /* Border */
    for (uint32_t i = 0; i < size; i++) {
        for (uint32_t j = 0; j < size; j++) {
            /* Simple rounded rect check */
            bool is_border = (i < 4 || i >= size-4 || j < 4 || j >= size-4);
            if (is_border && (x+i) < fb_width && (y+j) < fb_height) {
                framebuffer[(y+j) * (fb_pitch/4) + (x+i)] = color;
            }
        }
    }
}

void zepra_session_draw_launcher(void) {
    if (!framebuffer) return;
    
    serial_puts("[SESSION] Drawing launcher screen...\n");
    g_session_state = SESSION_RELAUNCH;
    
    /* Dark background */
    uint32_t bg_color = 0x00101020;
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            framebuffer[y * (fb_pitch/4) + x] = bg_color;
        }
    }
    
    /* Center icon */
    uint32_t icon_size = 120;
    uint32_t cx = fb_width / 2;
    uint32_t cy = fb_height / 2 - 40;
    
    draw_launcher_icon(cx, cy, icon_size, 0x0060E0FF);  /* Cyan icon */
    
    /* "Click to launch ZepraBrowser" text (simple placeholder) */
    /* In full implementation, would use nxf_draw_centered() from nxfont.h */
    
    serial_puts("[SESSION] Launcher ready - click icon to relaunch\n");
}

/* ============================================================================
 * NXRender Integration
 * ============================================================================ */

void zepra_nxrender_init(uint32_t width, uint32_t height) {
    if (g_nxrender_initialized) return;
    
    serial_puts("[NXRENDER] Initializing NXRender for browser...\n");
    
    /* 
     * NOTE: In full implementation, this initializes the NXRender subsystem
     * using reox_nxrender_bridge.h functions:
     *   rx_bridge_init(width, height);
     * 
     * For now, we just mark as initialized.
     */
    
    g_nxrender_initialized = true;
    serial_puts("[NXRENDER] NXRender ready\n");
}

void zepra_nxrender_frame(void) {
    if (!g_nxrender_initialized) return;
    
    /*
     * In full implementation, this calls:
     *   rx_frame();  // From reox_nxrender_bridge.h
     */
}

void zepra_nxrender_shutdown(void) {
    if (!g_nxrender_initialized) return;
    
    serial_puts("[NXRENDER] Shutting down NXRender...\n");
    
    /*
     * In full implementation, this calls:
     *   rx_bridge_destroy();
     */
    
    g_nxrender_initialized = false;
    serial_puts("[NXRENDER] NXRender shutdown complete\n");
}

/* Stub for network_shutdown if not defined elsewhere */
__attribute__((weak)) int network_shutdown(void) {
    serial_puts("[NETWORK] Network shutdown (stub)\n");
    return 0;
}

/* Stub for vfs_sync_all if not defined elsewhere */
__attribute__((weak)) void vfs_sync_all(void) {
    serial_puts("[VFS] Filesystem sync (stub)\n");
}
