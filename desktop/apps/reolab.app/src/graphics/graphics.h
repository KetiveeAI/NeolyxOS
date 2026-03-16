/*
 * Reolab - Graphics Backend
 * NeolyxOS Application
 * 
 * Abstraction layer for graphics rendering.
 * Uses SDL2 for development, NXRender on NeolyxOS.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef REOLAB_GRAPHICS_H
#define REOLAB_GRAPHICS_H

#include "../../include/reolab.h"

/* Color structure */
typedef struct {
    uint8_t r, g, b, a;
} RLColor;

/* Rectangle structure */
typedef struct {
    float x, y, w, h;
} RLRect;

/* Graphics context */
typedef struct RLGraphics RLGraphics;

/* Lifecycle */
RLGraphics* rl_graphics_create(const char* title, int width, int height);
void rl_graphics_destroy(RLGraphics* gfx);

/* Frame management */
void rl_graphics_begin_frame(RLGraphics* gfx);
void rl_graphics_end_frame(RLGraphics* gfx);
bool rl_graphics_poll_events(RLGraphics* gfx);
bool rl_graphics_should_quit(RLGraphics* gfx);

/* Drawing primitives */
void rl_graphics_clear(RLGraphics* gfx, RLColor color);
void rl_graphics_fill_rect(RLGraphics* gfx, RLRect rect, RLColor color);
void rl_graphics_fill_rounded_rect(RLGraphics* gfx, RLRect rect, RLColor color, float radius);
void rl_graphics_draw_line(RLGraphics* gfx, float x1, float y1, float x2, float y2, RLColor color);

/* Text rendering */
void rl_graphics_draw_text(RLGraphics* gfx, const char* text, float x, float y, float size, RLColor color);
void rl_graphics_measure_text(RLGraphics* gfx, const char* text, float size, float* width, float* height);

/* Input state */
void rl_graphics_get_mouse(RLGraphics* gfx, float* x, float* y);
bool rl_graphics_is_mouse_down(RLGraphics* gfx, int button);
bool rl_graphics_is_key_down(RLGraphics* gfx, int key);
const char* rl_graphics_get_text_input(RLGraphics* gfx);

/* Utility colors */
#define RL_COLOR(r,g,b,a) ((RLColor){r,g,b,a})
#define RL_RGB(r,g,b) ((RLColor){r,g,b,255})
#define RL_HEX(hex) ((RLColor){((hex)>>16)&0xFF, ((hex)>>8)&0xFF, (hex)&0xFF, 255})

/* ============================================= */
/*   ENHANCED IDE COLOR SCHEME (HIGH CONTRAST)  */
/* ============================================= */

/* Background colors - darker for more contrast */
#define RL_BG_DARK      RL_HEX(0x0D1117)  /* GitHub dark bg */
#define RL_BG_DARKER    RL_HEX(0x161B22)  /* Slightly lighter */
#define RL_BG_SIDEBAR   RL_HEX(0x21262D)  /* Sidebar */
#define RL_BG_EDITOR    RL_HEX(0x0D1117)  /* Editor area */
#define RL_BG_GUTTER    RL_HEX(0x161B22)  /* Line numbers */
#define RL_BG_ACTIVE    RL_HEX(0x1F6FEB)  /* Active selection */

/* Accent colors */
#define RL_ACCENT       RL_HEX(0x238636)  /* Green (like Run button) */
#define RL_ACCENT_BLUE  RL_HEX(0x1F6FEB)  /* Blue accent */
#define RL_ACCENT_PURP  RL_HEX(0x8957E5)  /* Purple accent */

/* Text colors - brighter for readability */
#define RL_TEXT         RL_HEX(0xE6EDF3)  /* Main text - bright! */
#define RL_TEXT_DIM     RL_HEX(0x7D8590)  /* Dimmed text */
#define RL_TEXT_BRIGHT  RL_HEX(0xFFFFFF)  /* Pure white */

/* Separator/border */
#define RL_BORDER       RL_HEX(0x30363D)  /* Borders */

/* ============================================= */
/*         SYNTAX HIGHLIGHTING COLORS           */
/* ============================================= */
#define RL_SYN_KEYWORD  RL_HEX(0xFF7B72)  /* Red-pink: if, for, while */
#define RL_SYN_TYPE     RL_HEX(0x79C0FF)  /* Blue: int, void, bool */
#define RL_SYN_FUNCTION RL_HEX(0xD2A8FF)  /* Purple: function names */
#define RL_SYN_STRING   RL_HEX(0xA5D6FF)  /* Light blue: strings */
#define RL_SYN_NUMBER   RL_HEX(0x79C0FF)  /* Blue: numbers */
#define RL_SYN_COMMENT  RL_HEX(0x8B949E)  /* Gray: comments */
#define RL_SYN_PREPROC  RL_HEX(0xFFA657)  /* Orange: #include, #define */
#define RL_SYN_MACRO    RL_HEX(0x79C0FF)  /* Blue: macros */
#define RL_SYN_OPERATOR RL_HEX(0xFF7B72)  /* Red: operators */

/* Cursor */
#define RL_CURSOR       RL_HEX(0x58A6FF)  /* Bright blue cursor */

#endif /* REOLAB_GRAPHICS_H */

