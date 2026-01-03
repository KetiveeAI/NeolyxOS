#include <video/video.h>
#include <stddef.h>

static volatile unsigned short* video_memory = (unsigned short*)VGA_BUFFER;
static int cursor_x = 0;
static int cursor_y = 0;
static unsigned char color = VGA_WHITE | (VGA_BLACK << 4);

void init_video(void) {
    cursor_x = 0;
    cursor_y = 0;
    color = VGA_WHITE | (VGA_BLACK << 4);
    clear_screen();
}

void clear_screen(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        video_memory[i] = (unsigned short)' ' | (unsigned short)color << 8;
    }
    cursor_x = 0;
    cursor_y = 0;
}

void print_char(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = 0;
        }
        return;
    }
    
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = 0;
        }
    }
    
    int index = cursor_y * VGA_WIDTH + cursor_x;
    video_memory[index] = (unsigned short)c | (unsigned short)color << 8;
    cursor_x++;
}

void print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        print_char(str[i]);
    }
} 