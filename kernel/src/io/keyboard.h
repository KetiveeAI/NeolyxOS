#ifndef KEYBOARD_H
#define KEYBOARD_H

// Keyboard functions
char keyboard_getchar(void);
void keyboard_init(void);

// Keyboard scancodes
#define KEY_ENTER 0x1C
#define KEY_BACKSPACE 0x0E
#define KEY_ESC 0x01

#endif // KEYBOARD_H 