/*
 * NeolyxOS Terminal.app - Main Entry Point
 * 
 * Desktop terminal emulator with tabs, copy/paste, and context menus.
 * Uses ReoxUI for native NeolyxOS look and feel.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>

/* ============ Forward Declarations ============ */

typedef struct terminal_app terminal_app_t;
typedef struct terminal_tab terminal_tab_t;

/* ReoxUI forward declarations */
typedef struct reox_window reox_window_t;
typedef struct reox_view reox_view_t;

/* ============ Terminal Configuration ============ */

#define TERM_COLS           80
#define TERM_ROWS           24
#define TERM_MAX_TABS       16
#define TERM_SCROLLBACK     1000

/* Colors */
#define TERM_FG_COLOR       0xE0E0E0
#define TERM_BG_COLOR       0x1A1A1A
#define TERM_CURSOR_COLOR   0x00FF88
#define TERM_SELECT_COLOR   0x404080

/* ============ Terminal Tab ============ */

typedef struct terminal_tab {
    int id;
    char title[64];
    
    /* Text buffer */
    char *buffer;           /* Display buffer (rows * cols) */
    char *scrollback;       /* Scrollback history */
    int scroll_offset;      /* Current scroll position */
    
    /* Cursor */
    int cursor_x;
    int cursor_y;
    int cursor_visible;
    
    /* Selection */
    int sel_start_x, sel_start_y;
    int sel_end_x, sel_end_y;
    int has_selection;
    
    /* Shell connection */
    int pty_fd;             /* Pseudo-terminal file descriptor */
    int shell_pid;          /* Shell process ID */
    
    /* State */
    int active;
    int modified;
} terminal_tab_t;

/* ============ Terminal Application ============ */

typedef struct terminal_app {
    /* Window */
    reox_window_t *window;
    int window_width;
    int window_height;
    
    /* Tabs */
    terminal_tab_t tabs[TERM_MAX_TABS];
    int tab_count;
    int active_tab;
    
    /* Font */
    int char_width;
    int char_height;
    uint32_t font_id;
    
    /* Clipboard */
    char *clipboard;
    int clipboard_len;
    
    /* State */
    int running;
    int needs_redraw;
} terminal_app_t;

/* ============ Function Prototypes ============ */

/* Application lifecycle */
int terminal_app_init(terminal_app_t *app);
void terminal_app_run(terminal_app_t *app);
void terminal_app_shutdown(terminal_app_t *app);

/* Tab management */
int terminal_tab_new(terminal_app_t *app);
void terminal_tab_close(terminal_app_t *app, int tab_id);
void terminal_tab_switch(terminal_app_t *app, int tab_id);

/* Drawing */
void terminal_draw(terminal_app_t *app);
void terminal_draw_tab_bar(terminal_app_t *app);
void terminal_draw_text(terminal_app_t *app, terminal_tab_t *tab);
void terminal_draw_cursor(terminal_app_t *app, terminal_tab_t *tab);
void terminal_draw_selection(terminal_app_t *app, terminal_tab_t *tab);

/* Input handling */
void terminal_on_key(terminal_app_t *app, uint16_t keycode, uint16_t mods);
void terminal_on_mouse(terminal_app_t *app, int x, int y, int button, int action);
void terminal_on_scroll(terminal_app_t *app, int delta);

/* Clipboard */
void terminal_copy(terminal_app_t *app);
void terminal_paste(terminal_app_t *app);

/* Context menu */
void terminal_show_context_menu(terminal_app_t *app, int x, int y);

/* Shell integration */
int terminal_spawn_shell(terminal_tab_t *tab);
void terminal_write_to_shell(terminal_tab_t *tab, const char *data, int len);
void terminal_read_from_shell(terminal_tab_t *tab);

/* ============ Application Entry Point ============ */

static terminal_app_t g_app;

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    /* Initialize terminal application */
    if (terminal_app_init(&g_app) != 0) {
        return 1;
    }
    
    /* Create initial tab */
    if (terminal_tab_new(&g_app) < 0) {
        terminal_app_shutdown(&g_app);
        return 1;
    }
    
    /* Run main loop */
    terminal_app_run(&g_app);
    
    /* Cleanup */
    terminal_app_shutdown(&g_app);
    
    return 0;
}

/* ============ Application Implementation ============ */

int terminal_app_init(terminal_app_t *app) {
    /* Zero initialize */
    for (int i = 0; i < (int)sizeof(*app); i++) {
        ((char *)app)[i] = 0;
    }
    
    /* Set defaults */
    app->window_width = 720;
    app->window_height = 480;
    app->char_width = 8;
    app->char_height = 16;
    app->running = 1;
    
    /* TODO: Initialize ReoxUI window */
    /* app->window = reox_window_create("Terminal", app->window_width, app->window_height); */
    
    return 0;
}

void terminal_app_run(terminal_app_t *app) {
    while (app->running) {
        /* Read shell output */
        if (app->tab_count > 0 && app->active_tab >= 0) {
            terminal_tab_t *tab = &app->tabs[app->active_tab];
            if (tab->active) {
                terminal_read_from_shell(tab);
            }
        }
        
        /* Draw if needed */
        if (app->needs_redraw) {
            terminal_draw(app);
            app->needs_redraw = 0;
        }
        
        /* TODO: Process ReoxUI events */
        /* reox_process_events(); */
    }
}

void terminal_app_shutdown(terminal_app_t *app) {
    /* Close all tabs */
    for (int i = 0; i < app->tab_count; i++) {
        if (app->tabs[i].active) {
            terminal_tab_close(app, i);
        }
    }
    
    /* Free clipboard */
    if (app->clipboard) {
        /* kfree(app->clipboard); */
        app->clipboard = NULL;
    }
    
    /* TODO: Destroy ReoxUI window */
    /* reox_window_destroy(app->window); */
}

/* ============ Tab Management ============ */

int terminal_tab_new(terminal_app_t *app) {
    if (app->tab_count >= TERM_MAX_TABS) {
        return -1;
    }
    
    int id = app->tab_count++;
    terminal_tab_t *tab = &app->tabs[id];
    
    tab->id = id;
    tab->cursor_x = 0;
    tab->cursor_y = 0;
    tab->cursor_visible = 1;
    tab->has_selection = 0;
    tab->scroll_offset = 0;
    tab->active = 1;
    
    /* Set title */
    const char *title = "bash";
    for (int i = 0; title[i] && i < 63; i++) {
        tab->title[i] = title[i];
        tab->title[i + 1] = '\0';
    }
    
    /* TODO: Allocate buffer */
    /* tab->buffer = kmalloc(TERM_COLS * TERM_ROWS); */
    
    /* TODO: Spawn shell */
    /* terminal_spawn_shell(tab); */
    
    app->active_tab = id;
    app->needs_redraw = 1;
    
    return id;
}

void terminal_tab_close(terminal_app_t *app, int tab_id) {
    if (tab_id < 0 || tab_id >= app->tab_count) return;
    
    terminal_tab_t *tab = &app->tabs[tab_id];
    
    /* Kill shell process */
    if (tab->shell_pid > 0) {
        /* kill(tab->shell_pid, SIGTERM); */
    }
    
    /* Close PTY */
    if (tab->pty_fd >= 0) {
        /* close(tab->pty_fd); */
    }
    
    /* Free buffer */
    if (tab->buffer) {
        /* kfree(tab->buffer); */
        tab->buffer = NULL;
    }
    
    tab->active = 0;
    
    /* Switch to another tab */
    if (app->active_tab == tab_id) {
        for (int i = 0; i < app->tab_count; i++) {
            if (app->tabs[i].active) {
                app->active_tab = i;
                break;
            }
        }
    }
    
    app->needs_redraw = 1;
}

void terminal_tab_switch(terminal_app_t *app, int tab_id) {
    if (tab_id >= 0 && tab_id < app->tab_count && app->tabs[tab_id].active) {
        app->active_tab = tab_id;
        app->needs_redraw = 1;
    }
}

/* ============ Drawing ============ */

void terminal_draw(terminal_app_t *app) {
    if (!app->window) return;
    
    terminal_tab_t *tab = &app->tabs[app->active_tab];
    
    /* Draw components */
    terminal_draw_tab_bar(app);
    terminal_draw_text(app, tab);
    terminal_draw_selection(app, tab);
    terminal_draw_cursor(app, tab);
    
    /* TODO: Present to screen */
    /* reox_window_present(app->window); */
}

void terminal_draw_tab_bar(terminal_app_t *app) {
    /* TODO: Draw tab bar with ReoxUI */
    (void)app;
}

void terminal_draw_text(terminal_app_t *app, terminal_tab_t *tab) {
    /* TODO: Render text grid with ReoxUI */
    (void)app;
    (void)tab;
}

void terminal_draw_cursor(terminal_app_t *app, terminal_tab_t *tab) {
    /* TODO: Draw blinking cursor */
    (void)app;
    (void)tab;
}

void terminal_draw_selection(terminal_app_t *app, terminal_tab_t *tab) {
    /* TODO: Highlight selected text */
    (void)app;
    (void)tab;
}

/* ============ Input Handling ============ */

void terminal_on_key(terminal_app_t *app, uint16_t keycode, uint16_t mods) {
    terminal_tab_t *tab = &app->tabs[app->active_tab];
    
    /* Check for shortcuts */
    int ctrl = mods & 0x100;
    int shift = mods & 0x200;
    
    (void)shift;
    
    if (ctrl) {
        char c = keycode & 0xFF;
        switch (c) {
            case 'c': /* Ctrl+C: Copy or interrupt */
                if (tab->has_selection) {
                    terminal_copy(app);
                } else {
                    /* Send SIGINT to shell */
                    terminal_write_to_shell(tab, "\x03", 1);
                }
                return;
            case 'v': /* Ctrl+V: Paste */
                terminal_paste(app);
                return;
            case 't': /* Ctrl+T: New tab */
                terminal_tab_new(app);
                return;
            case 'w': /* Ctrl+W: Close tab */
                terminal_tab_close(app, app->active_tab);
                return;
            case 'k': /* Ctrl+K: Clear */
                /* Clear screen */
                terminal_write_to_shell(tab, "\x1b[2J\x1b[H", 7);
                return;
        }
    }
    
    /* Send to shell */
    char buf[4];
    buf[0] = keycode & 0xFF;
    buf[1] = 0;
    terminal_write_to_shell(tab, buf, 1);
    
    app->needs_redraw = 1;
}

void terminal_on_mouse(terminal_app_t *app, int x, int y, int button, int action) {
    /* Right click - show context menu */
    if (button == 2 && action == 1) { /* Right button pressed */
        terminal_show_context_menu(app, x, y);
        return;
    }
    
    /* TODO: Handle selection */
    (void)app;
    (void)x;
    (void)y;
    (void)button;
    (void)action;
}

void terminal_on_scroll(terminal_app_t *app, int delta) {
    terminal_tab_t *tab = &app->tabs[app->active_tab];
    
    tab->scroll_offset += delta * 3;
    if (tab->scroll_offset < 0) tab->scroll_offset = 0;
    if (tab->scroll_offset > TERM_SCROLLBACK) tab->scroll_offset = TERM_SCROLLBACK;
    
    app->needs_redraw = 1;
}

/* ============ Clipboard ============ */

void terminal_copy(terminal_app_t *app) {
    terminal_tab_t *tab = &app->tabs[app->active_tab];
    
    if (!tab->has_selection) return;
    
    /* TODO: Extract selected text and copy to clipboard */
    (void)app;
}

void terminal_paste(terminal_app_t *app) {
    if (!app->clipboard || app->clipboard_len == 0) return;
    
    terminal_tab_t *tab = &app->tabs[app->active_tab];
    terminal_write_to_shell(tab, app->clipboard, app->clipboard_len);
}

/* ============ Context Menu ============ */

void terminal_show_context_menu(terminal_app_t *app, int x, int y) {
    /* TODO: Show ReoxUI popup menu with:
     * - Copy (if selection)
     * - Paste
     * - Clear
     * - Select All
     * - ---
     * - New Tab
     * - Close Tab
     */
    (void)app;
    (void)x;
    (void)y;
}

/* ============ Shell Integration ============ */

int terminal_spawn_shell(terminal_tab_t *tab) {
    /* TODO: Create PTY and fork shell process */
    (void)tab;
    return 0;
}

void terminal_write_to_shell(terminal_tab_t *tab, const char *data, int len) {
    /* TODO: Write to PTY */
    (void)tab;
    (void)data;
    (void)len;
}

void terminal_read_from_shell(terminal_tab_t *tab) {
    /* TODO: Read from PTY and update buffer */
    (void)tab;
}
