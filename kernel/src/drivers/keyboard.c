/*
 * NeolyxOS Keyboard Driver Implementation
 * 
 * PS/2 keyboard with IRQ1 handling.
 * Full scancode-to-ASCII with shift/caps/ctrl support.
 * Ring buffer for key input.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "drivers/keyboard.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
/* Note: pic_send_eoi is NOT needed here - EOI handled by ISR dispatcher */

/* I/O port access */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ============ Keyboard Ports ============ */

#define KBD_DATA_PORT       0x60
#define KBD_STATUS_PORT     0x64

/* ============ Keyboard State ============ */

/* Modifier key states */
static volatile int kbd_shift = 0;
static volatile int kbd_ctrl = 0;
static volatile int kbd_alt = 0;
static volatile int kbd_caps = 0;

/* Key buffer (ring buffer) */
#define KEY_BUFFER_SIZE     64
static volatile uint16_t key_buffer[KEY_BUFFER_SIZE];
static volatile int key_buffer_head = 0;
static volatile int key_buffer_tail = 0;

/* Last raw scancode */
static volatile uint8_t last_scancode = 0;

/* ============ Scancode Tables ============ */

/* Normal scancode to ASCII */
static const char kbd_map_normal[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Shifted scancode to ASCII */
static const char kbd_map_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* ============ Buffer Operations ============ */

static void buffer_push(uint16_t key) {
    int next = (key_buffer_head + 1) % KEY_BUFFER_SIZE;
    if (next != key_buffer_tail) {
        key_buffer[key_buffer_head] = key;
        key_buffer_head = next;
    }
    /* Buffer full - drop key */
}

static uint16_t buffer_pop(void) {
    if (key_buffer_head == key_buffer_tail) {
        return 0;  /* Empty */
    }
    uint16_t key = key_buffer[key_buffer_tail];
    key_buffer_tail = (key_buffer_tail + 1) % KEY_BUFFER_SIZE;
    return key;
}

/* ============ Keyboard Implementation ============ */

void keyboard_init(void) {
    serial_puts("[KBD] Keyboard driver initialized\n");
    key_buffer_head = 0;
    key_buffer_tail = 0;
    kbd_shift = 0;
    kbd_ctrl = 0;
    kbd_alt = 0;
    kbd_caps = 0;
}

void keyboard_irq(void) {
    uint8_t scancode = inb(KBD_DATA_PORT);
    last_scancode = scancode;
    
    int released = scancode & 0x80;
    uint8_t key = scancode & 0x7F;
    
    /* Handle modifier keys */
    switch (key) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
            kbd_shift = !released;
            goto done;
        case KEY_LCTRL:
            kbd_ctrl = !released;
            goto done;
        case KEY_LALT:
            kbd_alt = !released;
            goto done;
        case KEY_CAPSLOCK:
            if (!released) kbd_caps = !kbd_caps;
            goto done;
    }
    
    /* Only process key press, not release */
    if (released) goto done;
    
    /* Convert scancode to ASCII */
    char c;
    if (kbd_shift) {
        c = kbd_map_shift[key];
    } else {
        c = kbd_map_normal[key];
    }
    
    /* Apply Caps Lock for letters */
    if (kbd_caps) {
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        } else if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
    }
    
    /* Build key code with modifiers */
    uint16_t keycode = c;
    if (kbd_shift) keycode |= KEY_FLAG_SHIFT;
    if (kbd_ctrl) keycode |= KEY_FLAG_CTRL;
    if (kbd_alt) keycode |= KEY_FLAG_ALT;
    if (kbd_caps) keycode |= KEY_FLAG_CAPSLOCK;
    
    /* Special keys without ASCII representation */
    if (c == 0) {
        switch (key) {
            case KEY_UP:
            case KEY_DOWN:
            case KEY_LEFT:
            case KEY_RIGHT:
            case KEY_HOME:
            case KEY_END:
            case KEY_PAGEUP:
            case KEY_PAGEDOWN:
            case KEY_INSERT:
            case KEY_DELETE:
            case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4:
            case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
            case KEY_F9: case KEY_F10: case KEY_F11: case KEY_F12:
                keycode = key | 0x100;  /* Mark as special key */
                break;
            default:
                goto done;  /* Ignore unknown keys */
        }
    }
    
    /* Push to buffer */
    if (keycode) {
        buffer_push(keycode);
    }
    
done:
    /* EOI handled by ISR dispatcher - do not send here */
    return;
}

int keyboard_has_key(void) {
    return key_buffer_head != key_buffer_tail;
}

uint16_t keyboard_get_key(void) {
    /* Blocking wait */
    while (!keyboard_has_key()) {
        __asm__ volatile("hlt");
    }
    return buffer_pop();
}

uint16_t keyboard_get_key_nb(void) {
    return buffer_pop();
}

uint16_t keyboard_get_modifiers(void) {
    uint16_t mods = 0;
    if (kbd_shift) mods |= KEY_FLAG_SHIFT;
    if (kbd_ctrl) mods |= KEY_FLAG_CTRL;
    if (kbd_alt) mods |= KEY_FLAG_ALT;
    if (kbd_caps) mods |= KEY_FLAG_CAPSLOCK;
    return mods;
}

uint8_t keyboard_get_scancode(void) {
    return last_scancode;
}

/* ============ Legacy API Compatibility ============ */

/*
 * These functions provide backwards compatibility with code
 * that uses the old polling-style keyboard API.
 */

/* Check if a key is available (non-blocking) */
int kbd_check_key(void) {
    return keyboard_has_key();
}

/* Get raw scancode (waits for key if none pending) */
uint8_t kbd_getchar(void) {
    /* Wait for key */
    while (!keyboard_has_key()) {
        __asm__ volatile("hlt");
    }
    /* Return the scancode, not the ASCII */
    return last_scancode;
}

/* Initialize keyboard (compatibility) */
int kbd_init(void) {
    keyboard_init();
    return 0;
}

/* Blocking getchar that returns ASCII */
char keyboard_getchar(void) {
    uint16_t keycode = keyboard_get_key();
    /* If it's a special key, return 0 */
    if (keycode & 0x100) return 0;
    return (char)(keycode & 0xFF);
}

/* Check if Caps Lock is currently on */
int keyboard_is_capslock(void) {
    return kbd_caps;
}

/* ============ Syscall Interface ============ */

/**
 * keyboard_get_event - Get keyboard event for syscall
 * Called by sys_input_poll syscall to get keyboard events
 * Returns 0 if event available, -1 if no events
 */
int keyboard_get_event(uint8_t *scancode, uint8_t *ascii, uint8_t *pressed) {
    if (!keyboard_has_key()) {
        return -1;  /* No event */
    }
    
    uint16_t keycode = buffer_pop();
    if (keycode == 0) {
        return -1;  /* Empty */
    }
    
    /* Extract scancode from last_scancode */
    *scancode = last_scancode & 0x7F;
    
    /* Extract ASCII from keycode (lower 8 bits) */
    if (keycode & 0x100) {
        *ascii = 0;  /* Special key */
    } else {
        *ascii = (uint8_t)(keycode & 0xFF);
    }
    
    /* Check if it was a release (last_scancode high bit) */
    *pressed = !(last_scancode & 0x80);
    
    return 0;
}
