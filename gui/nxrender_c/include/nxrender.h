/*
 * NeolyxOS - NXRender GUI Framework
 * Unified header for kernel integration
 * 
 * Copyright (c) 2025 KetiveeAI
 * 
 * This is the main header to include for NXRender.
 * Include this single file to get access to all NXRender APIs.
 *
 * Usage:
 *   #include <nxrender.h>
 *
 *   // Initialize with kernel framebuffer
 *   nx_context_t* ctx = nxgfx_init(fb_addr, width, height, pitch);
 *   
 *   // Create window manager
 *   nx_window_manager_t* wm = nx_wm_create(ctx);
 *   
 *   // Create window with button
 *   nx_window_t* win = nx_window_create("My App", nx_rect(100, 100, 400, 300), 0);
 *   nx_button_t* btn = nx_button_create("Click Me");
 *   nx_window_set_root_widget(win, (nx_widget_t*)btn);
 *   nx_wm_add_window(wm, win);
 */

#ifndef NXRENDER_H
#define NXRENDER_H

/* Core Graphics */
#include "nxgfx/nxgfx.h"
#include "nxgfx/lighting.h"
#include "nxgfx/gpu_driver.h"

/* Core Rendering */
#include "nxrender/compositor.h"
#include "nxrender/window.h"
#include "nxrender/application.h"

/* Widgets */
#include "widgets/widget.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/textfield.h"
#include "widgets/container.h"

/* Layout */
#include "layout/layout.h"

/* Theme */
#include "theme/theme.h"

/* Animation */
#include "animation/animation.h"
#include "animation/animation_advanced.h"

/* Version info */
#define NXRENDER_VERSION_MAJOR 1
#define NXRENDER_VERSION_MINOR 0
#define NXRENDER_VERSION_PATCH 0
#define NXRENDER_VERSION_STRING "1.0.0"

#endif /* NXRENDER_H */
