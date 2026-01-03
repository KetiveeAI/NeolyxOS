#include "keyboard.h"
#include "io.h"
#include "video.h"

// US QWERTY scancode to ASCII (no shift, no control)
static const char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', /* 0x0E: Backspace */
    '\t', /* 0x0F: Tab */
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', /* 0x1C: Enter */
    0, /* Control */
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, /* Left Shift */
    '\\','z','x','c','v','b','n','m',',','.','/', 0, /* Right Shift */
    '*', 0, /* Alt */
    ' ', /* Space */
    0, /* CapsLock */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

char keyboard_getchar(void) {
    while (1) {
        uint8_t scancode = inb(0x60);
        if (scancode & 0x80) continue; // Ignore key releases
        char c = scancode_ascii[scancode];
        if (c) return c;
    }
} 