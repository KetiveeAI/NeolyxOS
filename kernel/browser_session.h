/*
 * NeolyxOS Browser Session Manager
 * 
 * PURPOSE: Manages ZepraBrowser as the primary NeolyxOS interface
 * 
 * SESSION LIFECYCLE:
 * 1. Boot firmware calls zepra_session_start()
 * 2. Browser runs as session manager
 * 3. On browser close:
 *    - If install requested → yield to installer
 *    - If shutdown → disconnect network + trigger shutdown
 *    - If re-launch → return to launcher screen
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef BROWSER_SESSION_H
#define BROWSER_SESSION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Session states */
typedef enum {
    SESSION_IDLE,           /* Browser not running */
    SESSION_RUNNING,        /* Browser is active */
    SESSION_INSTALL,        /* Browser yielded to installer */
    SESSION_SHUTDOWN,       /* Shutdown requested */
    SESSION_RELAUNCH        /* Show launcher for re-open */
} BrowserSessionState;

/* Session close actions */
typedef enum {
    CLOSE_ACTION_SHUTDOWN,  /* Disconnect + shutdown system */
    CLOSE_ACTION_RELAUNCH,  /* Return to launcher screen */
    CLOSE_ACTION_INSTALL    /* Yield to installer */
} BrowserCloseAction;

/* Session configuration from boot firmware */
typedef struct {
    uint64_t framebuffer_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    bool network_enabled;
} BrowserSessionConfig;

/* ============================================================================
 * Session Lifecycle API
 * ============================================================================ */

/**
 * Initialize browser session manager
 * Called by boot firmware after framebuffer is ready
 */
void zepra_session_init(BrowserSessionConfig* config);

/**
 * Start browser session
 * Launches ZepraBrowser as primary interface
 * Returns when browser requests close
 */
BrowserCloseAction zepra_session_start(void);

/**
 * Handle browser close based on action
 */
void zepra_session_close(BrowserCloseAction action);

/**
 * Request installer (called when user clicks "Install NeolyxOS" in browser)
 * Browser yields control to installer
 */
void zepra_session_request_install(void);

/**
 * Request shutdown (called when user closes browser)
 * Disconnects network and triggers system shutdown
 */
void zepra_session_shutdown(void);

/**
 * Get current session state
 */
BrowserSessionState zepra_session_state(void);

/**
 * Draw launcher screen (click to open browser icon)
 * Shown when browser is closed but system not shutting down
 */
void zepra_session_draw_launcher(void);

/* ============================================================================
 * NXRender Integration (from reox_nxrender_bridge.h)
 * ============================================================================ */

/**
 * Initialize NXRender for browser UI
 * Uses REOX bridge for UI components
 */
void zepra_nxrender_init(uint32_t width, uint32_t height);

/**
 * Render one frame using NXRender
 */
void zepra_nxrender_frame(void);

/**
 * Shutdown NXRender
 */
void zepra_nxrender_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* BROWSER_SESSION_H */
