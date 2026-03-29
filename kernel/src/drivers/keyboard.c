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

/* Extended scancode prefix (0xE0) tracking */
static volatile int kbd_e0_prefix = 0;

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
    serial_puts("[KBD] Keyboard driver initialized (extended scancode support)\n");
    key_buffer_head = 0;
    key_buffer_tail = 0;
    kbd_shift = 0;
    kbd_ctrl = 0;
    kbd_alt = 0;
    kbd_caps = 0;
    kbd_e0_prefix = 0;
}

void keyboard_irq(void) {
    uint8_t scancode = inb(KBD_DATA_PORT);
    last_scancode = scancode;
    
    /* Extended scancode prefix: 0xE0 precedes arrow/nav/meta keys.
     * Set flag and wait for the actual scancode on next IRQ. */
    if (scancode == 0xE0) {
        kbd_e0_prefix = 1;
        return;
    }
    
    int released = scancode & 0x80;
    uint8_t key = scancode & 0x7F;
    int is_extended = kbd_e0_prefix;
    kbd_e0_prefix = 0;
    
    /* Handle extended modifier keys (right-Ctrl, right-Alt, Meta) */
    if (is_extended) {
        switch (key) {
            case 0x1D:  /* Right Ctrl (E0 1D) */
                kbd_ctrl = !released;
                return;
            case 0x38:  /* Right Alt (E0 38) */
                kbd_alt = !released;
                return;
            case 0x5B:  /* Left Meta/Super (E0 5B) */
            case 0x5C:  /* Right Meta/Super (E0 5C) */
                return;
        }
        
        /* Extended keys: only process press, not release */
        if (released) return;
        
        /* Map extended scancodes to special key codes.
         * These share raw scancodes with numpad but the 0xE0 prefix
         * distinguishes them. Mark with 0x100 (special key flag). */
        uint16_t keycode = 0;
        switch (key) {
            case 0x48: keycode = KEY_UP     | 0x100; break;
            case 0x50: keycode = KEY_DOWN   | 0x100; break;
            case 0x4B: keycode = KEY_LEFT   | 0x100; break;
            case 0x4D: keycode = KEY_RIGHT  | 0x100; break;
            case 0x47: keycode = KEY_HOME   | 0x100; break;
            case 0x4F: keycode = KEY_END    | 0x100; break;
            case 0x49: keycode = KEY_PAGEUP | 0x100; break;
            case 0x51: keycode = KEY_PAGEDOWN | 0x100; break;
            case 0x52: keycode = KEY_INSERT | 0x100; break;
            case 0x53: keycode = KEY_DELETE | 0x100; break;
            default: return;
        }
        if (kbd_shift) keycode |= KEY_FLAG_SHIFT;
        if (kbd_ctrl)  keycode |= KEY_FLAG_CTRL;
        if (kbd_alt)   keycode |= KEY_FLAG_ALT;
        buffer_push(keycode);
        return;
    }
    
    /* Standard (non-extended) modifier keys */
    switch (key) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
            kbd_shift = !released;
            return;
        case KEY_LCTRL:
            kbd_ctrl = !released;
            return;
        case KEY_LALT:
            kbd_alt = !released;
            return;
        case KEY_CAPSLOCK:
            if (!released) kbd_caps = !kbd_caps;
            return;
    }
    
    /* Only process key press, not release */
    if (released) return;
    
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
    
    /* Function keys (non-ASCII, non-extended) */
    if (c == 0) {
        switch (key) {
            case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4:
            case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
            case KEY_F9: case KEY_F10: case KEY_F11: case KEY_F12:
                keycode = key | 0x100;
                break;
            default:
                return;
        }
    }
    
    if (keycode) {
        buffer_push(keycode);
    }
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
