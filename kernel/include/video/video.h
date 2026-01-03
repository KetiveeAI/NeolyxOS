// kernel/video.h
#ifndef VIDEO_H
#define VIDEO_H

// Video initialization
void init_video(void);

// Text output
void print(const char* str);
void print_char(char c);
void clear_screen(void);

// Video buffer
#define VIDEO_MEMORY 0xB8000
#define VIDEO_WIDTH 80
#define VIDEO_HEIGHT 25

// Aliases for compatibility
#define VGA_BUFFER VIDEO_MEMORY
#define VGA_WIDTH VIDEO_WIDTH
#define VGA_HEIGHT VIDEO_HEIGHT

// Colors
#define VGA_BLACK 0
#define VGA_WHITE 15
#define VGA_GREEN 2
#define VGA_BLUE 1

#endif // VIDEO_H 