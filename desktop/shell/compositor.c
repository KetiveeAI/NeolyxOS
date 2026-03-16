/*
 * NeolyxOS Desktop Environment Implementation
 * 
 * Window manager, taskbar, app launcher, desktop icons.
 * Modern, GPU-accelerated desktop via framebuffer.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "../include/desktop.h"

/* External dependencies from kernel */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void* kzalloc(uint64_t size);
extern void kfree(void *ptr);

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint32_t fb_width, fb_height, fb_pitch;
extern volatile uint32_t *framebuffer;
extern uint64_t pit_get_ticks(void);

/* For mouse */
extern int mouse_poll(int8_t *dx, int8_t *dy, uint8_t *buttons);

/* ============ Desktop State ============ */

static desktop_state_t desktop;
static uint32_t next_window_id = 1;

/* Desktop icons */
#define MAX_ICONS 20
static desktop_icon_t icons[MAX_ICONS];
static int icon_count = 0;

/* ============ Colors ============ */

#define COLOR_DESKTOP_BG    0x1A1A2E
#define COLOR_TASKBAR       0x16213E
#define COLOR_TASKBAR_BTN   0x0F3460
#define COLOR_WINDOW_TITLE  0x0F3460
#define COLOR_WINDOW_BG     0x1A1A2E
#define COLOR_WINDOW_BORDER 0xE94560
#define COLOR_ACCENT        0xE94560
#define COLOR_TEXT          0xFFFFFF
#define COLOR_BUTTON        0x0F3460
#define COLOR_BUTTON_HOVER  0x16213E

/* ============ Drawing Primitives ============ */

static void fb_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h && j < (int)fb_height; j++) {
        if (j < 0) continue;
        for (int i = x; i < x + w && i < (int)fb_width; i++) {
            if (i >= 0) {
                framebuffer[j * (fb_pitch / 4) + i] = color;
            }
        }
    }
}

static void fb_border(int x, int y, int w, int h, uint32_t color, int thickness) {
    /* Top */
    fb_rect(x, y, w, thickness, color);
    /* Bottom */
    fb_rect(x, y + h - thickness, w, thickness, color);
    /* Left */
    fb_rect(x, y, thickness, h, color);
    /* Right */
    fb_rect(x + w - thickness, y, thickness, h, color);
}

static void fb_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= (int)fb_height) continue;
        
        /* Interpolate colors */
        int t = (j * 255) / h;
        int r = (((c1 >> 16) & 0xFF) * (255 - t) + ((c2 >> 16) & 0xFF) * t) / 255;
        int g = (((c1 >> 8) & 0xFF) * (255 - t) + ((c2 >> 8) & 0xFF) * t) / 255;
        int b = ((c1 & 0xFF) * (255 - t) + (c2 & 0xFF) * t) / 255;
        uint32_t color = (r << 16) | (g << 8) | b;
        
        for (int i = x; i < x + w && i < (int)fb_width; i++) {
            if (i >= 0) {
                framebuffer[yy * (fb_pitch / 4) + i] = color;
            }
        }
    }
}

/* Simple 8x8 character rendering (placeholder) */
static void fb_char(int x, int y, char c, uint32_t color) {
    /* Very simple representation - just draw a block for now */
    if (c >= 'A' && c <= 'Z') {
        fb_rect(x + 1, y + 1, 5, 6, color);
    } else if (c >= 'a' && c <= 'z') {
        fb_rect(x + 1, y + 2, 5, 5, color);
    } else if (c >= '0' && c <= '9') {
        fb_rect(x + 1, y + 1, 5, 6, color);
    } else if (c != ' ') {
        fb_rect(x + 2, y + 2, 3, 4, color);
    }
}

static void fb_text(int x, int y, const char *text, uint32_t color) {
    while (*text) {
        fb_char(x, y, *text, color);
        x += 8;
        text++;
    }
}

/* ============ Mouse Cursor ============ */

static void draw_cursor(int x, int y) {
    /* Simple arrow cursor */
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j <= i && j < 8; j++) {
            int px = x + j;
            int py = y + i;
            if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                framebuffer[py * (fb_pitch / 4) + px] = COLOR_TEXT;
            }
        }
    }
    /* Border */
    for (int i = 0; i < 12; i++) {
        int px = x;
        int py = y + i;
        if (py >= 0 && py < (int)fb_height) {
            framebuffer[py * (fb_pitch / 4) + px] = 0x000000;
        }
    }
}

/* ============ Taskbar ============ */

void taskbar_draw(void) {
    int taskbar_h = 40;
    int taskbar_y = fb_height - taskbar_h;
    
    /* Taskbar background with gradient */
    fb_gradient_v(0, taskbar_y, fb_width, taskbar_h, 0x16213E, 0x0F3460);
    
    /* Start button */
    fb_rect(5, taskbar_y + 5, 80, 30, COLOR_ACCENT);
    fb_text(15, taskbar_y + 13, "NeoLyx", COLOR_TEXT);
    
    /* Window buttons */
    int btn_x = 100;
    desktop_window_t *win = desktop.windows;
    while (win) {
        uint32_t btn_color = (win == desktop.focused_window) ? COLOR_ACCENT : COLOR_TASKBAR_BTN;
        fb_rect(btn_x, taskbar_y + 5, 120, 30, btn_color);
        fb_text(btn_x + 10, taskbar_y + 13, win->title, COLOR_TEXT);
        btn_x += 130;
        win = win->next;
    }
    
    /* Clock */
    uint64_t ticks = pit_get_ticks();
    int secs = (ticks / 1000) % 60;
    int mins = (ticks / 60000) % 60;
    int hours = (ticks / 3600000) % 24;
    
    char time_str[16];
    time_str[0] = '0' + (hours / 10);
    time_str[1] = '0' + (hours % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (mins / 10);
    time_str[4] = '0' + (mins % 10);
    time_str[5] = ':';
    time_str[6] = '0' + (secs / 10);
    time_str[7] = '0' + (secs % 10);
    time_str[8] = '\0';
    
    fb_text(fb_width - 80, taskbar_y + 13, time_str, COLOR_TEXT);
}

void taskbar_click(int x, int y) {
    int taskbar_h = 40;
    int taskbar_y = fb_height - taskbar_h;
    
    if (y < taskbar_y) return;
    
    /* Start button */
    if (x >= 5 && x <= 85) {
        launcher_show();
        return;
    }
    
    /* Window buttons */
    int btn_x = 100;
    desktop_window_t *win = desktop.windows;
    while (win) {
        if (x >= btn_x && x <= btn_x + 120) {
            desktop_focus_window(win);
            return;
        }
        btn_x += 130;
        win = win->next;
    }
}

/* ============ Window Manager ============ */

static void draw_window(desktop_window_t *win) {
    if (win->minimized) return;
    
    int title_h = 30;
    
    /* Window shadow */
    fb_rect(win->x + 4, win->y + 4, win->width, win->height + title_h, 0x000000);
    
    /* Window border */
    uint32_t border_color = (win == desktop.focused_window) ? COLOR_ACCENT : COLOR_WINDOW_BORDER;
    fb_rect(win->x - 2, win->y - 2, win->width + 4, win->height + title_h + 4, border_color);
    
    /* Title bar */
    uint32_t title_color = (win == desktop.focused_window) ? COLOR_WINDOW_TITLE : 0x2A2A4E;
    fb_rect(win->x, win->y, win->width, title_h, title_color);
    fb_text(win->x + 10, win->y + 8, win->title, COLOR_TEXT);
    
    /* Close button */
    fb_rect(win->x + win->width - 25, win->y + 5, 20, 20, COLOR_ACCENT);
    fb_text(win->x + win->width - 20, win->y + 8, "X", COLOR_TEXT);
    
    /* Minimize button */
    fb_rect(win->x + win->width - 50, win->y + 5, 20, 20, COLOR_BUTTON);
    fb_text(win->x + win->width - 45, win->y + 8, "-", COLOR_TEXT);
    
    /* Window content area */
    fb_rect(win->x, win->y + title_h, win->width, win->height, COLOR_WINDOW_BG);
}

desktop_window_t *desktop_create_window(const char *title, int x, int y, int width, int height) {
    desktop_window_t *win = (desktop_window_t *)kzalloc(sizeof(desktop_window_t));
    if (!win) return NULL;
    
    win->id = next_window_id++;
    
    /* Copy title */
    int i = 0;
    while (title[i] && i < 127) { win->title[i] = title[i]; i++; }
    win->title[i] = '\0';
    
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->focused = 1;
    
    /* Add to window list */
    win->next = desktop.windows;
    desktop.windows = win;
    desktop.focused_window = win;
    
    serial_puts("[DESKTOP] Created window: ");
    serial_puts(title);
    serial_puts("\n");
    
    return win;
}

void desktop_destroy_window(desktop_window_t *window) {
    if (!window) return;
    
    /* Remove from list */
    if (desktop.windows == window) {
        desktop.windows = window->next;
    } else {
        desktop_window_t *prev = desktop.windows;
        while (prev && prev->next != window) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = window->next;
        }
    }
    
    if (desktop.focused_window == window) {
        desktop.focused_window = desktop.windows;
    }
    
    kfree(window);
}

void desktop_focus_window(desktop_window_t *window) {
    if (!window) return;
    
    /* Unfocus old */
    if (desktop.focused_window) {
        desktop.focused_window->focused = 0;
    }
    
    /* Focus new and move to front */
    window->focused = 1;
    
    /* Remove from list */
    if (desktop.windows != window) {
        desktop_window_t *prev = desktop.windows;
        while (prev && prev->next != window) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = window->next;
            window->next = desktop.windows;
            desktop.windows = window;
        }
    }
    
    desktop.focused_window = window;
}

/* ============ App Launcher ============ */

static int launcher_visible = 0;

void launcher_show(void) {
    launcher_visible = 1;
}

void launcher_hide(void) {
    launcher_visible = 0;
}

static void launcher_draw(void) {
    if (!launcher_visible) return;
    
    int w = 300;
    int h = 400;
    int x = 10;
    int y = fb_height - 40 - h - 10;
    
    /* Background */
    fb_rect(x, y, w, h, 0x16213E);
    fb_border(x, y, w, h, COLOR_ACCENT, 2);
    
    /* Title */
    fb_text(x + 20, y + 15, "Applications", COLOR_TEXT);
    
    /* App list */
    const char *apps[] = {"Terminal", "Settings", "File Manager", "Text Editor", "About NeolyxOS"};
    for (int i = 0; i < 5; i++) {
        fb_rect(x + 10, y + 50 + i * 45, w - 20, 40, COLOR_BUTTON);
        fb_text(x + 60, y + 65 + i * 45, apps[i], COLOR_TEXT);
    }
}

int launcher_run_app(const char *path) {
    /* TODO: Start process */
    (void)path;
    return 0;
}

/* ============ Desktop Icons ============ */

int desktop_add_icon(const char *name, const char *icon, const char *exec) {
    if (icon_count >= MAX_ICONS) return -1;
    
    desktop_icon_t *ic = &icons[icon_count];
    
    /* Copy strings */
    int i = 0;
    while (name[i] && i < 63) { ic->name[i] = name[i]; i++; }
    ic->name[i] = '\0';
    
    i = 0;
    while (exec[i] && i < 255) { ic->exec_path[i] = exec[i]; i++; }
    ic->exec_path[i] = '\0';
    
    (void)icon;  /* TODO: Load icon */
    
    /* Position */
    ic->x = 20 + (icon_count % 4) * 100;
    ic->y = 20 + (icon_count / 4) * 100;
    
    icon_count++;
    return 0;
}

static void desktop_icons_draw(void) {
    for (int i = 0; i < icon_count; i++) {
        desktop_icon_t *ic = &icons[i];
        
        /* Icon placeholder */
        fb_rect(ic->x, ic->y, 64, 64, COLOR_BUTTON);
        fb_border(ic->x, ic->y, 64, 64, COLOR_ACCENT, 1);
        
        /* Label */
        fb_text(ic->x, ic->y + 70, ic->name, COLOR_TEXT);
    }
}

void desktop_icon_click(int x, int y) {
    for (int i = 0; i < icon_count; i++) {
        desktop_icon_t *ic = &icons[i];
        if (x >= ic->x && x <= ic->x + 64 && y >= ic->y && y <= ic->y + 64) {
            launcher_run_app(ic->exec_path);
            return;
        }
    }
}

/* ============ Desktop Main ============ */

void desktop_draw(void) {
    /* Desktop background */
    fb_gradient_v(0, 0, fb_width, fb_height - 40, COLOR_DESKTOP_BG, 0x0F0F1A);
    
    /* Desktop icons */
    desktop_icons_draw();
    
    /* Windows (back to front) */
    /* First find last window */
    desktop_window_t *last = NULL;
    desktop_window_t *win = desktop.windows;
    while (win) {
        last = win;
        win = win->next;
    }
    
    /* Draw from last to first (so first is on top) */
    win = last;
    while (win) {
        draw_window(win);
        /* Find previous */
        if (win == desktop.windows) break;
        desktop_window_t *prev = desktop.windows;
        while (prev->next != win) prev = prev->next;
        win = prev;
    }
    
    /* Launcher */
    launcher_draw();
    
    /* Taskbar */
    taskbar_draw();
    
    /* Mouse cursor */
    draw_cursor(desktop.mouse_x, desktop.mouse_y);
}

void desktop_handle_input(void) {
    /* Mouse */
    int8_t dx = 0, dy = 0;
    uint8_t buttons = 0;
    
    if (mouse_poll(&dx, &dy, &buttons)) {
        desktop.mouse_x += dx;
        desktop.mouse_y -= dy;  /* Invert Y */
        
        /* Clamp */
        if (desktop.mouse_x < 0) desktop.mouse_x = 0;
        if (desktop.mouse_x >= (int)fb_width) desktop.mouse_x = fb_width - 1;
        if (desktop.mouse_y < 0) desktop.mouse_y = 0;
        if (desktop.mouse_y >= (int)fb_height) desktop.mouse_y = fb_height - 1;
        
        /* Left click */
        if ((buttons & 0x01) && !(desktop.mouse_buttons & 0x01)) {
            /* Check taskbar */
            if (desktop.mouse_y >= (int)(fb_height - 40)) {
                taskbar_click(desktop.mouse_x, desktop.mouse_y);
            }
            /* Check launcher */
            else if (launcher_visible) {
                /* TODO: Handle launcher click */
                launcher_hide();
            }
            /* Check icons */
            else if (desktop.mouse_y < (int)(fb_height - 40) - 100) {
                desktop_icon_click(desktop.mouse_x, desktop.mouse_y);
            }
        }
        
        desktop.mouse_buttons = buttons;
    }
    
    /* Keyboard */
    if (keyboard_has_key()) {
        uint16_t key = keyboard_get_key_nb();
        
        /* Super key opens launcher */
        if (key == 0x5B || key == 0x5C) {
            if (launcher_visible) launcher_hide();
            else launcher_show();
        }
    }
}

int desktop_init(void) {
    serial_puts("[DESKTOP] Initializing desktop environment...\n");
    
    /* Initialize state */
    desktop.config.screen_width = fb_width;
    desktop.config.screen_height = fb_height;
    desktop.config.taskbar_height = 40;
    desktop.config.bg_color = COLOR_DESKTOP_BG;
    desktop.config.dark_mode = 1;
    
    desktop.windows = NULL;
    desktop.focused_window = NULL;
    desktop.mouse_x = fb_width / 2;
    desktop.mouse_y = fb_height / 2;
    desktop.running = 1;
    
    /* Add default icons */
    desktop_add_icon("Terminal", "", "/apps/terminal");
    desktop_add_icon("Files", "", "/apps/files");
    desktop_add_icon("Settings", "", "/apps/settings");
    
    /* Create welcome window */
    desktop_window_t *welcome = desktop_create_window("Welcome to NeolyxOS", 
                                                       fb_width/2 - 200, fb_height/2 - 150,
                                                       400, 250);
    (void)welcome;
    
    serial_puts("[DESKTOP] Ready\n");
    return 0;
}

/* DISABLED - using desktop_shell.c version instead */
#if 0
void compositor_run_legacy(void) {
    
    while (desktop.running) {
        desktop_handle_input();
        desktop_draw();
        
        /* Small delay */
        for (volatile int i = 0; i < 100000; i++);
    }
    
    /* Should never reach here */
    while (1) __asm__ volatile("hlt");
}
#endif /* disabled compositor_run_legacy */
