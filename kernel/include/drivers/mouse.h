/*
 * NeolyxOS Complete Mouse Handler
 * Supports: Movement, 8 buttons, scroll wheel
 * PS/2 Mouse Protocol with Intellimouse Extensions
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stddef.h>

/* Mouse button flags */
#define MOUSE_BTN_LEFT      (1 << 0)
#define MOUSE_BTN_RIGHT     (1 << 1)
#define MOUSE_BTN_MIDDLE    (1 << 2)
#define MOUSE_BTN_4         (1 << 3)
#define MOUSE_BTN_5         (1 << 4)
#define MOUSE_BTN_6         (1 << 5)
#define MOUSE_BTN_7         (1 << 6)
#define MOUSE_BTN_8         (1 << 7)

/* Legacy button definitions (compatibility) */
#define MOUSE_BUTTON_LEFT     MOUSE_BTN_LEFT
#define MOUSE_BUTTON_RIGHT    MOUSE_BTN_RIGHT
#define MOUSE_BUTTON_MIDDLE   MOUSE_BTN_MIDDLE

/* Mouse IRQ */
#define MOUSE_IRQ       12
#define MOUSE_INT_VEC   44

/* Mouse state structure */
typedef struct {
    int32_t x;                  /* Absolute X position */
    int32_t y;                  /* Absolute Y position */
    int16_t delta_x;            /* Movement delta X */
    int16_t delta_y;            /* Movement delta Y */
    int8_t  scroll_vertical;    /* Scroll wheel (vertical) */
    int8_t  scroll_horizontal;  /* Scroll wheel (horizontal) */
    uint8_t buttons;            /* Button state */
    uint8_t buttons_prev;       /* Previous button state */
    uint8_t mouse_id;           /* Mouse device ID (0=std, 3=scroll, 4=5btn) */
    uint8_t packet_size;        /* Packet size (3, 4, or 5 bytes) */
    int     initialized;        /* 1 if initialized */
} mouse_state_t;

/* ============================================================
 * Kernel Driver API (kdrv format)
 * Note: kernel_driver_t is defined in drivers.h
 * ============================================================ */
#ifndef DRIVERS_H
struct kernel_driver;
#endif

extern int mouse_driver_init(struct kernel_driver* drv);
extern int mouse_driver_deinit(struct kernel_driver* drv);

/* ============================================================
 * Common Mouse Operations
 * ============================================================ */

/* Get current mouse state */
const mouse_state_t* mouse_get_state(void);

/* Check if button is currently pressed */
int mouse_button_pressed(uint8_t button);

/* Check if button was just pressed (edge detection) */
int mouse_button_clicked(uint8_t button);

/* Check if button was just released */
int mouse_button_released(uint8_t button);

/* Get mouse position */
int mouse_get_position(int* x, int* y);

/* Set mouse position */
int mouse_set_position(int x, int y);

/* Get mouse button state */
int mouse_get_buttons(void);

/* Check if specific button is pressed */
int mouse_is_button_pressed(int button);

/* Get mouse movement delta */
int mouse_get_movement(int* dx, int* dy);

/* Get scroll wheel delta (clears after read) */
int8_t mouse_get_scroll_vertical(void);

/* Enable/disable mouse */
int mouse_set_enabled(int enabled);

/* Check if mouse is ready */
int mouse_is_ready(void);

/* Reset mouse state */
void mouse_reset_state(void);

/* Mouse interrupt handler (IRQ12) */
void mouse_interrupt_handler(void);

#endif /* MOUSE_H */
