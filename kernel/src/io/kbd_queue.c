/*
 * NeolyxOS Keyboard Event Queue Implementation
 * 
 * Ring buffer for keyboard events from IRQ handler.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "io/kbd_queue.h"

/* External dependencies */
extern void serial_puts(const char *s);
extern uint64_t pit_get_ticks(void);

/* I/O port access */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ============ Ring Buffer ============ */

#define KBD_QUEUE_SIZE 64

static kbd_event_t event_queue[KBD_QUEUE_SIZE];
static volatile int queue_head = 0;
static volatile int queue_tail = 0;

/* Modifier state */
static uint8_t mod_shift = 0;
static uint8_t mod_ctrl = 0;
static uint8_t mod_alt = 0;
static uint8_t mod_caps = 0;

/* ============ Scancode to ASCII Table ============ */

static const char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_to_ascii_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* ============ Queue Operations ============ */

void kbd_queue_init(void) {
    queue_head = 0;
    queue_tail = 0;
    mod_shift = mod_ctrl = mod_alt = mod_caps = 0;
    serial_puts("[KBD_QUEUE] Initialized\n");
}

static void queue_push(kbd_event_t *evt) {
    int next_head = (queue_head + 1) % KBD_QUEUE_SIZE;
    
    /* Drop if full */
    if (next_head == queue_tail) {
        return;
    }
    
    event_queue[queue_head] = *evt;
    queue_head = next_head;
}

int kbd_queue_pop(kbd_event_t *out) {
    if (queue_tail == queue_head) {
        return 0;  /* Empty */
    }
    
    *out = event_queue[queue_tail];
    queue_tail = (queue_tail + 1) % KBD_QUEUE_SIZE;
    return 1;
}

int kbd_queue_peek(kbd_event_t *out) {
    if (queue_tail == queue_head) {
        return 0;  /* Empty */
    }
    
    *out = event_queue[queue_tail];
    return 1;
}

int kbd_queue_has_event(void) {
    return queue_tail != queue_head;
}

void kbd_queue_clear(void) {
    queue_head = 0;
    queue_tail = 0;
}

/* ============ Scancode to Character ============ */

char kbd_event_to_char(kbd_event_t *evt) {
    uint8_t sc = evt->scancode;
    
    /* Key release - ignore */
    if (sc & 0x80) {
        return 0;
    }
    
    if (sc >= 128) return 0;
    
    /* Shift or capslock for letters */
    int use_shift = (evt->flags & KBD_FLAG_SHIFT) != 0;
    
    /* Capslock toggles for letters */
    char c;
    if (use_shift) {
        c = scancode_to_ascii_shift[sc];
    } else {
        c = scancode_to_ascii[sc];
    }
    
    /* Apply capslock to letters */
    if ((evt->flags & KBD_FLAG_CAPS) && c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    } else if ((evt->flags & KBD_FLAG_CAPS) && c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
    }
    
    return c;
}

/* ============ IRQ Handler ============ */

void kbd_irq_handler(void) {
    uint8_t scancode = inb(0x60);
    
    /* Handle modifier keys */
    int is_release = (scancode & 0x80) != 0;
    uint8_t key = scancode & 0x7F;
    
    switch (key) {
        case 0x2A: case 0x36:  /* Left/Right Shift */
            mod_shift = !is_release;
            return;
        case 0x1D:  /* Ctrl */
            mod_ctrl = !is_release;
            return;
        case 0x38:  /* Alt */
            mod_alt = !is_release;
            return;
        case 0x3A:  /* Caps Lock */
            if (!is_release) {
                mod_caps = !mod_caps;
            }
            return;
    }
    
    /* Only queue key presses, not releases */
    if (!is_release) {
        kbd_event_t evt;
        evt.scancode = scancode;
        evt.flags = 0;
        if (mod_shift) evt.flags |= KBD_FLAG_SHIFT;
        if (mod_ctrl) evt.flags |= KBD_FLAG_CTRL;
        if (mod_alt) evt.flags |= KBD_FLAG_ALT;
        if (mod_caps) evt.flags |= KBD_FLAG_CAPS;
        evt.timestamp = pit_get_ticks();
        
        queue_push(&evt);
    }
}
