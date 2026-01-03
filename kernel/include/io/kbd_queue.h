/*
 * NeolyxOS Keyboard Event Queue Header
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYXOS_KBD_QUEUE_H
#define NEOLYXOS_KBD_QUEUE_H

#include <stdint.h>

/* Keyboard event structure */
typedef struct {
    uint8_t scancode;
    uint8_t flags;
    uint64_t timestamp;
} kbd_event_t;

/* Modifier flags */
#define KBD_FLAG_SHIFT  0x01
#define KBD_FLAG_CTRL   0x02
#define KBD_FLAG_ALT    0x04
#define KBD_FLAG_CAPS   0x08

/* Initialize keyboard queue */
void kbd_queue_init(void);

/* Pop next event from queue (returns 1 if available, 0 if empty) */
int kbd_queue_pop(kbd_event_t *out);

/* Peek next event without removing (returns 1 if available, 0 if empty) */
int kbd_queue_peek(kbd_event_t *out);

/* Check if events are waiting */
int kbd_queue_has_event(void);

/* Clear all pending events */
void kbd_queue_clear(void);

/* Convert event to ASCII character (returns 0 if not printable) */
char kbd_event_to_char(kbd_event_t *evt);

/* IRQ handler (called from IDT) */
void kbd_irq_handler(void);

#endif /* NEOLYXOS_KBD_QUEUE_H */
