/*
 * NeolyxOS - Setup Assistant (first-run registration and setup flow)
 *
 * A full-screen layer drawn above everything except the cursor while
 * active. desktop_shell.c calls these hooks; while active, the assistant
 * receives all mouse and keyboard input.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef SETUP_ASSISTANT_H
#define SETUP_ASSISTANT_H

#include <stdint.h>
#include "nxcanvas.h"

/* Decide at boot whether setup must run (no completed-setup marker). */
void setup_assistant_init(void);
int  setup_assistant_active(void);
/* Force the flow to start from the first page (Settings "Run setup again",
 * preview scripts). */
void setup_assistant_show(void);
void setup_assistant_tick(float dt);
void setup_assistant_render(nxc_canvas_t *c);
/* Return 1 if consumed (always 1 while active). */
int  setup_assistant_handle_mouse(int32_t x, int32_t y, uint8_t buttons, uint8_t prev_buttons);
int  setup_assistant_handle_key(uint8_t scancode, uint8_t pressed, char ascii);

#endif /* SETUP_ASSISTANT_H */
