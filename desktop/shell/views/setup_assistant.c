/*
 * NeolyxOS - Setup Assistant (owned by the Setup flow area)
 *
 * Placeholder: never active. The Setup flow area replaces this file.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "setup_assistant.h"

static int g_active;

void setup_assistant_init(void) { g_active = 0; }
int  setup_assistant_active(void) { return g_active; }
void setup_assistant_show(void) { g_active = 0; }
void setup_assistant_tick(float dt) { (void)dt; }
void setup_assistant_render(nxc_canvas_t *c) { (void)c; }
int  setup_assistant_handle_mouse(int32_t x, int32_t y, uint8_t b, uint8_t pb) {
    (void)x; (void)y; (void)b; (void)pb;
    return g_active;
}
int  setup_assistant_handle_key(uint8_t sc, uint8_t pressed, char ascii) {
    (void)sc; (void)pressed; (void)ascii;
    return g_active;
}
