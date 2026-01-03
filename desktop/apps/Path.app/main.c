/*
 * NeolyxOS Path.app - Main Entry Point
 * 
 * Desktop file manager with sidebar, toolbar, and multiple views.
 * Uses NXRender for native NeolyxOS look and VFS syscalls for file access.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"

/* ============ Forward Declarations ============ */

typedef struct files_app files_app_t;
typedef struct file_entry file_entry_t;
typedef struct folder_path folder_path_t;

/* VFS operations from vfs_ops.c */
extern int path_vfs_list_dir(const char *path, file_entry_t *entries, int max);
extern int path_vfs_stat(const char *path, file_entry_t *entry);
extern int path_vfs_mkdir(const char *path);
extern int path_vfs_touch(const char *path);
extern int path_vfs_delete(const char *path);
extern int path_vfs_rename(const char *old_path, const char *new_path);
extern int path_vfs_copy(const char *src, const char *dst);
extern int path_vfs_move(const char *src, const char *dst);
extern const char* path_basename(const char *path);
extern const char* path_extension(const char *filename);
extern int path_is_image(const char *filename);

/* Component forward declarations */
typedef struct toolbar toolbar_t;
typedef struct sidebar sidebar_t;
typedef struct tab_bar tab_bar_t;
typedef struct context_menu context_menu_t;
typedef struct quick_look quick_look_t;
typedef struct status_bar status_bar_t;
typedef struct drag_state drag_state_t;
typedef struct search_state search_state_t;

/* Component externs (from their respective .c files) */
extern void toolbar_init(toolbar_t *toolbar, int height);
extern void toolbar_draw(void *ctx, toolbar_t *toolbar, int x, int y, int width);
extern void toolbar_set_path(toolbar_t *toolbar, const char *path);
extern void toolbar_set_navigation(toolbar_t *toolbar, int can_back, int can_forward);
extern void toolbar_set_view(toolbar_t *toolbar, int mode);
extern int toolbar_hit_test(toolbar_t *toolbar, int x, int y, int toolbar_x, int toolbar_y, int width);
extern void toolbar_on_hover(toolbar_t *toolbar, int button);

extern void sidebar_init(sidebar_t *sidebar, int width);
extern void sidebar_draw(void *ctx, sidebar_t *sidebar, int x, int y, int height);
extern void sidebar_add_favorite(sidebar_t *sidebar, const char *name, const char *path);
extern void sidebar_add_volume(sidebar_t *sidebar, const char *name, const char *path);
extern int sidebar_hit_test(sidebar_t *sidebar, int x, int y, int sidebar_x, int sidebar_y);
extern void sidebar_on_hover(sidebar_t *sidebar, int index);
extern const char* sidebar_on_click(sidebar_t *sidebar, int index);

extern void tab_bar_init(tab_bar_t *bar);
extern void tab_bar_draw(void *ctx, tab_bar_t *bar, int x, int y, int width);
extern int tab_bar_new_tab(tab_bar_t *bar, const char *path);
extern void tab_bar_close_tab(tab_bar_t *bar, int idx);
extern void tab_bar_switch_tab(tab_bar_t *bar, int idx);
extern void tab_bar_update_title(tab_bar_t *bar, int idx, const char *path);
extern int tab_bar_on_key(tab_bar_t *bar, uint16_t keycode, uint16_t mods, const char *current_path);

extern void context_menu_init(context_menu_t *menu);
extern void context_menu_draw(void *ctx, context_menu_t *menu);
extern void context_menu_show(context_menu_t *menu, int x, int y, int has_sel, int has_clip, int is_dir);
extern void context_menu_hide(context_menu_t *menu);
extern int context_menu_hit_test(context_menu_t *menu, int x, int y);
extern int context_menu_on_click(context_menu_t *menu, int index);

extern void quick_look_init(quick_look_t *ql);
extern void quick_look_draw(void *ctx, quick_look_t *ql);
extern void quick_look_toggle(quick_look_t *ql, const char *path, const char *name, uint64_t size, int is_dir, int screen_w, int screen_h);
extern void quick_look_hide(quick_look_t *ql);
extern int quick_look_on_key(quick_look_t *ql, uint16_t keycode);

extern void status_bar_init(status_bar_t *bar, int height);
extern void status_bar_draw(void *ctx, status_bar_t *bar, int x, int y, int width);
extern void status_bar_update(status_bar_t *bar, int items, int selected, uint64_t total, uint64_t sel_size);
extern void status_bar_set_message(status_bar_t *bar, const char *msg, int timeout);

extern void drag_init(drag_state_t *drag);
extern void drag_begin(drag_state_t *drag, int x, int y);
extern int drag_add_file(drag_state_t *drag, const char *path, const char *name);
extern int drag_update(drag_state_t *drag, int x, int y);
extern int drag_is_active(drag_state_t *drag);
extern void drag_cancel(drag_state_t *drag);
extern void drag_draw(void *ctx, drag_state_t *drag);

extern void search_init(search_state_t *search);
extern void search_start(search_state_t *search, const char *path, const char *query);
extern int search_step(search_state_t *search);
extern int search_is_active(search_state_t *search);
extern void search_cancel(search_state_t *search);
extern void search_clear(search_state_t *search);

extern void file_view_draw(void *ctx, file_entry_t *entries, int count, int mode, int icon_size, int x, int y, int width, int height);

/* ============ Configuration ============ */

#define FILES_MAX_PATH      1024
#define FILES_MAX_ENTRIES   256
#define FILES_MAX_FAVORITES 16
#define FILES_MAX_VOLUMES   8
#define FILES_MAX_HISTORY   32

#define TOOLBAR_HEIGHT      48
#define SIDEBAR_WIDTH       200
#define TAB_BAR_HEIGHT      32
#define STATUS_BAR_HEIGHT   24

/* View Modes */
typedef enum {
    VIEW_ICONS,
    VIEW_LIST,
    VIEW_COLUMNS,
    VIEW_GALLERY
} view_mode_t;

/* Sort Options */
typedef enum {
    SORT_NAME,
    SORT_DATE,
    SORT_SIZE,
    SORT_TYPE
} sort_mode_t;

/* ============ File Entry ============ */

typedef struct file_entry {
    char name[256];
    char path[FILES_MAX_PATH];
    uint64_t size;
    uint64_t mtime;
    uint32_t mode;
    uint8_t is_dir;
    uint8_t is_hidden;
    uint8_t is_symlink;
    uint8_t selected;
    uint32_t icon_id;
} file_entry_t;

/* ============ Folder Path ============ */

typedef struct folder_path {
    char path[FILES_MAX_PATH];
    char name[64];
} folder_path_t;

/* ============ Component Structures (placeholders for sizeof) ============ */

/* These match the actual structs in their respective .c files */
struct toolbar {
    int can_back, can_forward, can_up, view_mode, hover_button, height;
    char path[1024], search_query[256];
    int search_active;
};

struct sidebar {
    char favorites[16][320];  /* Simplified */
    int favorites_count;
    char volumes[8][320];
    int volumes_count;
    int hover_index, active_index, width;
};

struct context_menu {
    int visible, x, y, width, height, hover_index;
    int has_selection, has_clipboard, is_directory;
};

struct quick_look {
    int visible, x, y, width, height;
    char filename[256], filepath[1024];
    uint64_t filesize;
    int is_dir, preview_type;
    void *image_data;
    int image_width, image_height, item_count;
    char items[8][64];
};

struct status_bar {
    int item_count, selected_count;
    uint64_t total_size, selected_size;
    char message[128];
    int message_timeout, height;
};

struct drag_state {
    int mode;
    char source_paths[16][1024];
    char source_names[16][256];
    int file_count;
    int start_x, start_y, drag_x, drag_y;
    char target_path[1024];
    int drop_valid, drop_action;
    int offset_x, offset_y;
};

struct search_state {
    char query[256], base_path[1024];
    int case_sensitive, search_contents;
    file_entry_t results[256];
    int result_count, status;
    char pending_dirs[64][1024];
    int pending_count, current_depth, files_scanned;
};

struct tab_bar {
    char tabs[16][2048];  /* Simplified */
    int tab_count, active_tab, hover_tab, hover_close, height, visible;
};

/* ============ Files Application ============ */

typedef struct files_app {
    /* Window */
    void *window;
    int window_width;
    int window_height;
    
    /* Current directory */
    char current_path[FILES_MAX_PATH];
    file_entry_t entries[FILES_MAX_ENTRIES];
    int entry_count;
    int selected_count;
    uint64_t total_size;
    uint64_t selected_size;
    
    /* Navigation history */
    folder_path_t history[FILES_MAX_HISTORY];
    int history_count;
    int history_pos;
    
    /* Sidebar favorites */
    folder_path_t favorites[FILES_MAX_FAVORITES];
    int favorites_count;
    folder_path_t volumes[FILES_MAX_VOLUMES];
    int volumes_count;
    
    /* View settings */
    view_mode_t view_mode;
    sort_mode_t sort_mode;
    int show_hidden;
    int icon_size;
    
    /* Clipboard */
    char clipboard_path[FILES_MAX_PATH];
    int clipboard_cut;
    
    /* State */
    int running;
    int needs_redraw;
    int renaming;
    char rename_buffer[256];
    
    /* UI Components */
    struct toolbar toolbar;
    struct sidebar sidebar;
    struct tab_bar tabs;
    struct context_menu context_menu;
    struct quick_look quick_look;
    struct status_bar status_bar;
    struct drag_state drag;
    struct search_state search;
    
    /* Scroll offset */
    int scroll_y;
} files_app_t;

/* ============ String Helpers ============ */

static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void str_cat(char *dst, const char *src, int max) {
    int dlen = str_len(dst);
    int i = 0;
    while (src[i] && dlen + i < max - 1) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = '\0';
}

static char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int str_casecmp(const char *a, const char *b) {
    while (*a && *b) {
        char la = to_lower(*a);
        char lb = to_lower(*b);
        if (la != lb) return la - lb;
        a++; b++;
    }
    return to_lower(*a) - to_lower(*b);
}

/* ============ Function Prototypes ============ */

int files_app_init(files_app_t *app);
void files_app_run(files_app_t *app);
void files_app_shutdown(files_app_t *app);

int files_navigate(files_app_t *app, const char *path);
int files_go_up(files_app_t *app);
int files_go_back(files_app_t *app);
int files_go_forward(files_app_t *app);
int files_refresh(files_app_t *app);

int files_open_selected(files_app_t *app);
int files_copy(files_app_t *app);
int files_cut(files_app_t *app);
int files_paste(files_app_t *app);
int files_delete(files_app_t *app);
int files_rename(files_app_t *app, const char *new_name);
int files_new_folder(files_app_t *app);
int files_new_file(files_app_t *app);

void files_select(files_app_t *app, int index);
void files_select_all(files_app_t *app);
void files_deselect_all(files_app_t *app);
void files_toggle_selection(files_app_t *app, int index);

void files_set_view(files_app_t *app, view_mode_t mode);
void files_set_sort(files_app_t *app, sort_mode_t mode);
void files_toggle_hidden(files_app_t *app);

void files_draw(files_app_t *app);
void files_on_key(files_app_t *app, uint16_t keycode, uint16_t mods);
void files_on_mouse(files_app_t *app, int x, int y, int button, int action);
void files_on_mouse_move(files_app_t *app, int x, int y);

/* ============ Sorting ============ */

static int files_compare(files_app_t *app, file_entry_t *a, file_entry_t *b) {
    /* Directories first */
    if (a->is_dir != b->is_dir) {
        return b->is_dir - a->is_dir;
    }
    
    switch (app->sort_mode) {
        case SORT_NAME:
            return str_casecmp(a->name, b->name);
        case SORT_SIZE:
            if (a->size > b->size) return 1;
            if (a->size < b->size) return -1;
            return 0;
        case SORT_DATE:
            if (a->mtime > b->mtime) return -1;  /* Newest first */
            if (a->mtime < b->mtime) return 1;
            return 0;
        case SORT_TYPE: {
            const char *ext_a = path_extension(a->name);
            const char *ext_b = path_extension(b->name);
            int cmp = str_casecmp(ext_a, ext_b);
            if (cmp != 0) return cmp;
            return str_casecmp(a->name, b->name);
        }
    }
    return 0;
}

static void files_sort(files_app_t *app) {
    /* Insertion sort (stable, good for mostly-sorted data) */
    for (int i = 1; i < app->entry_count; i++) {
        file_entry_t temp = app->entries[i];
        int j = i - 1;
        
        while (j >= 0 && files_compare(app, &app->entries[j], &temp) > 0) {
            app->entries[j + 1] = app->entries[j];
            j--;
        }
        app->entries[j + 1] = temp;
    }
}

/* ============ Statistics Update ============ */

static void files_update_stats(files_app_t *app) {
    app->selected_count = 0;
    app->total_size = 0;
    app->selected_size = 0;
    
    for (int i = 0; i < app->entry_count; i++) {
        file_entry_t *e = &app->entries[i];
        if (!e->is_dir) {
            app->total_size += e->size;
        }
        if (e->selected) {
            app->selected_count++;
            if (!e->is_dir) {
                app->selected_size += e->size;
            }
        }
    }
    
    status_bar_update(&app->status_bar, app->entry_count, app->selected_count,
                      app->total_size, app->selected_size);
}

/* ============ Application Entry Point ============ */

static files_app_t g_app;

int main(int argc, char **argv) {
    (void)argc;
    
    if (files_app_init(&g_app) != 0) {
        return 1;
    }
    
    const char *start_path = "/Users";
    if (argc > 1 && argv[1]) {
        start_path = argv[1];
    }
    files_navigate(&g_app, start_path);
    
    files_app_run(&g_app);
    files_app_shutdown(&g_app);
    
    return 0;
}

/* ============ Application Implementation ============ */

int files_app_init(files_app_t *app) {
    /* Zero initialize */
    for (int i = 0; i < (int)sizeof(*app); i++) {
        ((char *)app)[i] = 0;
    }
    
    /* Set defaults */
    app->window_width = 900;
    app->window_height = 600;
    app->view_mode = VIEW_ICONS;
    app->sort_mode = SORT_NAME;
    app->icon_size = 64;
    app->show_hidden = 0;
    app->running = 1;
    app->scroll_y = 0;
    
    /* Initialize components */
    toolbar_init(&app->toolbar, TOOLBAR_HEIGHT);
    sidebar_init(&app->sidebar, SIDEBAR_WIDTH);
    tab_bar_init(&app->tabs);
    context_menu_init(&app->context_menu);
    quick_look_init(&app->quick_look);
    status_bar_init(&app->status_bar, STATUS_BAR_HEIGHT);
    drag_init(&app->drag);
    search_init(&app->search);
    
    /* Add default favorites */
    sidebar_add_favorite(&app->sidebar, "Home", "/Users");
    sidebar_add_favorite(&app->sidebar, "Desktop", "/Users/Desktop");
    sidebar_add_favorite(&app->sidebar, "Documents", "/Users/Documents");
    sidebar_add_favorite(&app->sidebar, "Applications", "/Applications");
    
    /* Add default volumes */
    sidebar_add_volume(&app->sidebar, "NeolyxOS", "/");
    
    /* Create initial tab */
    tab_bar_new_tab(&app->tabs, "/Users");
    
    return 0;
}

void files_app_run(files_app_t *app) {
    while (app->running) {
        /* Process events */
        nx_event_t event;
        while (nx_poll_event(app->window, &event)) {
            switch (event.type) {
                case NX_EVENT_KEY:
                    files_on_key(app, event.key.code, event.key.mods);
                    break;
                case NX_EVENT_MOUSE_MOVE:
                    files_on_mouse_move(app, event.mouse.x, event.mouse.y);
                    break;
                case NX_EVENT_MOUSE_BUTTON:
                    files_on_mouse(app, event.mouse.x, event.mouse.y,
                                   event.mouse.button, event.mouse.action);
                    break;
                case NX_EVENT_SCROLL:
                    app->scroll_y -= event.scroll.dy * 32;
                    if (app->scroll_y < 0) app->scroll_y = 0;
                    app->needs_redraw = 1;
                    break;
                case NX_EVENT_CLOSE:
                    app->running = 0;
                    break;
                case NX_EVENT_RESIZE:
                    app->window_width = event.resize.width;
                    app->window_height = event.resize.height;
                    app->needs_redraw = 1;
                    break;
            }
        }
        
        /* Process incremental search */
        if (search_is_active(&app->search)) {
            if (search_step(&app->search) > 0) {
                app->needs_redraw = 1;
            }
        }
        
        /* Redraw if needed */
        if (app->needs_redraw) {
            files_draw(app);
            nx_present(app->window);
            app->needs_redraw = 0;
        }
        
        /* Yield CPU */
        nx_sleep(16);
    }
}

void files_app_shutdown(files_app_t *app) {
    if (app->window) {
        nx_window_destroy(app->window);
        app->window = 0;
    }
}

/* ============ Navigation ============ */

int files_navigate(files_app_t *app, const char *path) {
    if (!path) return -1;
    
    str_copy(app->current_path, path, FILES_MAX_PATH);
    
    /* Add to history */
    if (app->history_pos < FILES_MAX_HISTORY - 1) {
        folder_path_t *h = &app->history[app->history_pos++];
        str_copy(h->path, path, FILES_MAX_PATH);
        app->history_count = app->history_pos;
    }
    
    /* Update toolbar */
    toolbar_set_path(&app->toolbar, path);
    toolbar_set_navigation(&app->toolbar, app->history_pos > 1, app->history_pos < app->history_count);
    
    /* Update tab title */
    tab_bar_update_title(&app->tabs, app->tabs.active_tab, path);
    
    /* Load directory */
    return files_refresh(app);
}

int files_go_up(files_app_t *app) {
    int last_slash = -1;
    for (int i = 0; app->current_path[i]; i++) {
        if (app->current_path[i] == '/') last_slash = i;
    }
    
    if (last_slash > 0) {
        char parent[FILES_MAX_PATH];
        for (int i = 0; i < last_slash; i++) {
            parent[i] = app->current_path[i];
        }
        parent[last_slash] = '\0';
        return files_navigate(app, parent);
    } else if (last_slash == 0) {
        return files_navigate(app, "/");
    }
    
    return -1;
}

int files_go_back(files_app_t *app) {
    if (app->history_pos > 1) {
        app->history_pos--;
        str_copy(app->current_path, app->history[app->history_pos - 1].path, FILES_MAX_PATH);
        toolbar_set_path(&app->toolbar, app->current_path);
        toolbar_set_navigation(&app->toolbar, app->history_pos > 1, app->history_pos < app->history_count);
        return files_refresh(app);
    }
    return -1;
}

int files_go_forward(files_app_t *app) {
    if (app->history_pos < app->history_count) {
        str_copy(app->current_path, app->history[app->history_pos].path, FILES_MAX_PATH);
        app->history_pos++;
        toolbar_set_path(&app->toolbar, app->current_path);
        toolbar_set_navigation(&app->toolbar, app->history_pos > 1, app->history_pos < app->history_count);
        return files_refresh(app);
    }
    return -1;
}

int files_refresh(files_app_t *app) {
    app->scroll_y = 0;
    files_deselect_all(app);
    
    /* Use VFS to read directory */
    int count = path_vfs_list_dir(app->current_path, app->entries, FILES_MAX_ENTRIES);
    
    if (count < 0) {
        app->entry_count = 0;
        status_bar_set_message(&app->status_bar, "Could not read directory", 3000);
    } else {
        app->entry_count = count;
        
        /* Filter hidden files if needed */
        if (!app->show_hidden) {
            int write_idx = 0;
            for (int i = 0; i < app->entry_count; i++) {
                if (!app->entries[i].is_hidden) {
                    if (write_idx != i) {
                        app->entries[write_idx] = app->entries[i];
                    }
                    write_idx++;
                }
            }
            app->entry_count = write_idx;
        }
        
        /* Sort entries */
        files_sort(app);
    }
    
    files_update_stats(app);
    app->needs_redraw = 1;
    return count >= 0 ? 0 : -1;
}

/* ============ File Operations ============ */

int files_open_selected(files_app_t *app) {
    for (int i = 0; i < app->entry_count; i++) {
        if (app->entries[i].selected) {
            file_entry_t *entry = &app->entries[i];
            
            if (entry->is_dir) {
                return files_navigate(app, entry->path);
            } else {
                /* Open file with default application based on extension */
                const char *ext = path_extension(entry->name);
                const char *app_path = NULL;
                
                /* Map extensions to default apps */
                if ((ext[0]=='t' && ext[1]=='x' && ext[2]=='t') ||
                    (ext[0]=='m' && ext[1]=='d') ||
                    (ext[0]=='c' && (ext[1]=='\0' || ext[1]=='p')) ||
                    (ext[0]=='h' && (ext[1]=='\0' || ext[1]=='p')) ||
                    (ext[0]=='j' && ext[1]=='s') ||
                    (ext[0]=='p' && ext[1]=='y')) {
                    app_path = "/Applications/TextEdit.app/bin/textedit";
                } else if ((ext[0]=='p' && ext[1]=='n' && ext[2]=='g') ||
                           (ext[0]=='j' && ext[1]=='p' && ext[2]=='g') ||
                           (ext[0]=='g' && ext[1]=='i' && ext[2]=='f')) {
                    app_path = "/Applications/Preview.app/bin/preview";
                }
                
                if (app_path) {
                    /* Launch application with file path */
                    /* Uses NeolyxOS exec syscall */
                    nx_exec(app_path, entry->path);
                    status_bar_set_message(&app->status_bar, "Opening file...", 2000);
                } else {
                    /* No default app - show Quick Look */
                    quick_look_toggle(&app->quick_look, entry->path, entry->name,
                                      entry->size, entry->is_dir,
                                      app->window_width, app->window_height);
                }
                app->needs_redraw = 1;
                return 0;
            }
        }
    }
    return -1;
}

int files_copy(files_app_t *app) {
    for (int i = 0; i < app->entry_count; i++) {
        if (app->entries[i].selected) {
            str_copy(app->clipboard_path, app->entries[i].path, FILES_MAX_PATH);
            app->clipboard_cut = 0;
            status_bar_set_message(&app->status_bar, "Copied to clipboard", 2000);
            app->needs_redraw = 1;
            return 0;
        }
    }
    return -1;
}

int files_cut(files_app_t *app) {
    if (files_copy(app) == 0) {
        app->clipboard_cut = 1;
        status_bar_set_message(&app->status_bar, "Cut to clipboard", 2000);
        return 0;
    }
    return -1;
}

int files_paste(files_app_t *app) {
    if (!app->clipboard_path[0]) return -1;
    
    /* Build destination path */
    char dst[FILES_MAX_PATH];
    str_copy(dst, app->current_path, FILES_MAX_PATH);
    if (dst[str_len(dst) - 1] != '/') {
        str_cat(dst, "/", FILES_MAX_PATH);
    }
    str_cat(dst, path_basename(app->clipboard_path), FILES_MAX_PATH);
    
    int result;
    if (app->clipboard_cut) {
        result = path_vfs_move(app->clipboard_path, dst);
        if (result == 0) {
            app->clipboard_path[0] = '\0';
            status_bar_set_message(&app->status_bar, "Moved successfully", 2000);
        }
    } else {
        result = path_vfs_copy(app->clipboard_path, dst);
        if (result == 0) {
            status_bar_set_message(&app->status_bar, "Copied successfully", 2000);
        }
    }
    
    if (result != 0) {
        status_bar_set_message(&app->status_bar, "Operation failed", 2000);
    }
    
    return files_refresh(app);
}

int files_delete(files_app_t *app) {
    int deleted = 0;
    
    for (int i = 0; i < app->entry_count; i++) {
        if (app->entries[i].selected) {
            if (path_vfs_delete(app->entries[i].path) == 0) {
                deleted++;
            }
        }
    }
    
    if (deleted > 0) {
        status_bar_set_message(&app->status_bar, "Deleted", 2000);
        return files_refresh(app);
    }
    return -1;
}

int files_rename(files_app_t *app, const char *new_name) {
    if (!new_name || !new_name[0]) return -1;
    
    for (int i = 0; i < app->entry_count; i++) {
        if (app->entries[i].selected) {
            char new_path[FILES_MAX_PATH];
            str_copy(new_path, app->current_path, FILES_MAX_PATH);
            if (new_path[str_len(new_path) - 1] != '/') {
                str_cat(new_path, "/", FILES_MAX_PATH);
            }
            str_cat(new_path, new_name, FILES_MAX_PATH);
            
            if (path_vfs_rename(app->entries[i].path, new_path) == 0) {
                status_bar_set_message(&app->status_bar, "Renamed", 2000);
                return files_refresh(app);
            }
            break;
        }
    }
    return -1;
}

/*
 * Open selected folder in Terminal.app
 * Launches Terminal with the directory path as working directory
 */
int files_open_in_terminal(files_app_t *app) {
    /* Get path to open - either selected folder or current directory */
    const char *target_path = app->current_path;
    
    for (int i = 0; i < app->entry_count; i++) {
        if (app->entries[i].selected) {
            if (app->entries[i].is_dir) {
                target_path = app->entries[i].path;
            } else {
                /* For files, use current directory */
                target_path = app->current_path;
            }
            break;
        }
    }
    
    /* Build Terminal.app launch path with directory argument */
    /* NeolyxOS app launching: /Applications/Terminal.app/bin/terminal <path> */
    char cmd_path[FILES_MAX_PATH + 64];
    str_copy(cmd_path, "/Applications/Terminal.app/bin/terminal", sizeof(cmd_path));
    
    /* Use NeolyxOS app launching syscall */
    /* nx_exec launches a new process with the given arguments */
    nx_exec(cmd_path, target_path);
    
    status_bar_set_message(&app->status_bar, "Opening Terminal...", 2000);
    app->needs_redraw = 1;
    
    return 0;
}

int files_new_folder(files_app_t *app) {
    char path[FILES_MAX_PATH];
    str_copy(path, app->current_path, FILES_MAX_PATH);
    if (path[str_len(path) - 1] != '/') {
        str_cat(path, "/", FILES_MAX_PATH);
    }
    str_cat(path, "New Folder", FILES_MAX_PATH);
    
    if (path_vfs_mkdir(path) == 0) {
        status_bar_set_message(&app->status_bar, "Folder created", 2000);
        return files_refresh(app);
    }
    return -1;
}


int files_new_file(files_app_t *app) {
    char path[FILES_MAX_PATH];
    str_copy(path, app->current_path, FILES_MAX_PATH);
    if (path[str_len(path) - 1] != '/') {
        str_cat(path, "/", FILES_MAX_PATH);
    }
    str_cat(path, "New File", FILES_MAX_PATH);
    
    if (path_vfs_touch(path) == 0) {
        status_bar_set_message(&app->status_bar, "File created", 2000);
        return files_refresh(app);
    }
    return -1;
}

/* ============ Selection ============ */

void files_select(files_app_t *app, int index) {
    files_deselect_all(app);
    if (index >= 0 && index < app->entry_count) {
        app->entries[index].selected = 1;
    }
    files_update_stats(app);
    app->needs_redraw = 1;
}

void files_toggle_selection(files_app_t *app, int index) {
    if (index >= 0 && index < app->entry_count) {
        app->entries[index].selected = !app->entries[index].selected;
    }
    files_update_stats(app);
    app->needs_redraw = 1;
}

void files_select_all(files_app_t *app) {
    for (int i = 0; i < app->entry_count; i++) {
        app->entries[i].selected = 1;
    }
    files_update_stats(app);
    app->needs_redraw = 1;
}

void files_deselect_all(files_app_t *app) {
    for (int i = 0; i < app->entry_count; i++) {
        app->entries[i].selected = 0;
    }
    files_update_stats(app);
    app->needs_redraw = 1;
}

/* ============ View Settings ============ */

void files_set_view(files_app_t *app, view_mode_t mode) {
    app->view_mode = mode;
    toolbar_set_view(&app->toolbar, mode);
    app->needs_redraw = 1;
}

void files_set_sort(files_app_t *app, sort_mode_t mode) {
    app->sort_mode = mode;
    files_sort(app);
    app->needs_redraw = 1;
}

void files_toggle_hidden(files_app_t *app) {
    app->show_hidden = !app->show_hidden;
    files_refresh(app);
}

/* ============ Drawing ============ */

void files_draw(files_app_t *app) {
    void *ctx = app->window;
    if (!ctx) return;
    
    int y = 0;
    
    /* Clear background */
    nx_fill_rect(ctx, 0, 0, app->window_width, app->window_height, 0x1E1E2E);
    
    /* Tab bar (if multiple tabs) */
    if (app->tabs.tab_count > 1) {
        tab_bar_draw(ctx, &app->tabs, 0, y, app->window_width);
        y += TAB_BAR_HEIGHT;
    }
    
    /* Toolbar */
    toolbar_draw(ctx, &app->toolbar, 0, y, app->window_width);
    y += TOOLBAR_HEIGHT;
    
    int content_height = app->window_height - y - STATUS_BAR_HEIGHT;
    
    /* Sidebar */
    sidebar_draw(ctx, &app->sidebar, 0, y, content_height);
    
    /* File view */
    int file_x = SIDEBAR_WIDTH;
    int file_w = app->window_width - SIDEBAR_WIDTH;
    
    /* Use search results if searching */
    if (search_is_active(&app->search) || app->search.result_count > 0) {
        file_view_draw(ctx, app->search.results, app->search.result_count,
                       app->view_mode, app->icon_size,
                       file_x, y, file_w, content_height);
    } else {
        file_view_draw(ctx, app->entries, app->entry_count,
                       app->view_mode, app->icon_size,
                       file_x, y, file_w, content_height);
    }
    
    /* Status bar */
    status_bar_draw(ctx, &app->status_bar, 0, app->window_height - STATUS_BAR_HEIGHT, app->window_width);
    
    /* Overlays */
    context_menu_draw(ctx, &app->context_menu);
    quick_look_draw(ctx, &app->quick_look);
    drag_draw(ctx, &app->drag);
}

/* ============ Input Handling ============ */

void files_on_key(files_app_t *app, uint16_t keycode, uint16_t mods) {
    int ctrl = mods & 0x02;
    int shift = mods & 0x01;
    
    /* Quick Look handles escape and space when visible */
    if (app->quick_look.visible) {
        if (quick_look_on_key(&app->quick_look, keycode)) {
            app->needs_redraw = 1;
            return;
        }
    }
    
    /* Tab bar shortcuts */
    if (tab_bar_on_key(&app->tabs, keycode, mods, app->current_path)) {
        app->needs_redraw = 1;
        return;
    }
    
    /* Global shortcuts */
    if (ctrl) {
        switch (keycode & 0xFF) {
            case 'c': case 'C': files_copy(app); return;
            case 'x': case 'X': files_cut(app); return;
            case 'v': case 'V': files_paste(app); return;
            case 'a': case 'A': files_select_all(app); return;
            case 'n': case 'N':
                if (shift) files_new_file(app);
                else files_new_folder(app);
                return;
            case 'r': case 'R': files_refresh(app); return;
            case 'h': case 'H': files_toggle_hidden(app); return;
            case 'i': case 'I':
                /* Get Info - trigger quick look for now */
                files_open_selected(app);
                return;
        }
    } else {
        switch (keycode) {
            case 0x0D: /* Enter */
                files_open_selected(app);
                break;
            case 0x7F: /* Delete/Backspace */
                files_delete(app);
                break;
            case 0x1B: /* Escape */
                context_menu_hide(&app->context_menu);
                search_cancel(&app->search);
                files_deselect_all(app);
                break;
            case ' ': /* Space - Quick Look */
                files_open_selected(app);
                break;
            case 0x26: /* Up arrow */
                files_go_up(app);
                break;
            case 0x25: /* Left arrow */
                files_go_back(app);
                break;
            case 0x27: /* Right arrow */
                files_go_forward(app);
                break;
            case '1': files_set_view(app, VIEW_ICONS); break;
            case '2': files_set_view(app, VIEW_LIST); break;
            case '3': files_set_view(app, VIEW_COLUMNS); break;
            case '4': files_set_view(app, VIEW_GALLERY); break;
        }
    }
    
    app->needs_redraw = 1;
}

void files_on_mouse_move(files_app_t *app, int x, int y) {
    /* Update drag if active */
    if (drag_is_active(&app->drag)) {
        drag_update(&app->drag, x, y);
        app->needs_redraw = 1;
    }
    
    /* Toolbar hover */
    int btn = toolbar_hit_test(&app->toolbar, x, y, 0, 
                                app->tabs.tab_count > 1 ? TAB_BAR_HEIGHT : 0,
                                app->window_width);
    toolbar_on_hover(&app->toolbar, btn);
    
    /* Sidebar hover */
    int sidebar_y = (app->tabs.tab_count > 1 ? TAB_BAR_HEIGHT : 0) + TOOLBAR_HEIGHT;
    int idx = sidebar_hit_test(&app->sidebar, x, y, 0, sidebar_y);
    sidebar_on_hover(&app->sidebar, idx);
    
    /* Context menu hover */
    if (app->context_menu.visible) {
        int menu_idx = context_menu_hit_test(&app->context_menu, x, y);
        app->context_menu.hover_index = menu_idx;
    }
}

void files_on_mouse(files_app_t *app, int x, int y, int button, int action) {
    int toolbar_y = app->tabs.tab_count > 1 ? TAB_BAR_HEIGHT : 0;
    int content_y = toolbar_y + TOOLBAR_HEIGHT;
    
    /* Handle context menu first */
    if (app->context_menu.visible) {
        if (action == 1) {  /* Click */
            int idx = context_menu_hit_test(&app->context_menu, x, y);
            if (idx >= 0) {
                int cmd = context_menu_on_click(&app->context_menu, idx);
                /* Handle menu command - indices match menu_item_t enum */
                switch (cmd) {
                    case 0: files_open_selected(app); break;       /* MENU_OPEN */
                    case 1: /* MENU_OPEN_WITH - not implemented */ break;
                    case 2: files_open_in_terminal(app); break;    /* MENU_OPEN_TERMINAL */
                    /* case 3: separator */
                    case 4: files_open_selected(app); break;       /* MENU_GET_INFO */
                    case 5: /* MENU_RENAME - not implemented */ break;
                    /* case 6: separator */
                    case 7: files_cut(app); break;                 /* MENU_CUT */
                    case 8: files_copy(app); break;                /* MENU_COPY */
                    case 9: files_paste(app); break;               /* MENU_PASTE */
                    /* case 10: separator */
                    case 11: files_delete(app); break;             /* MENU_MOVE_TRASH */
                    /* case 12: separator */
                    case 13: files_new_folder(app); break;         /* MENU_NEW_FOLDER */
                    case 14: files_new_file(app); break;           /* MENU_NEW_FILE */
                }

            } else {
                context_menu_hide(&app->context_menu);
            }
            app->needs_redraw = 1;
            return;
        }
    }
    
    /* Right click - show context menu */
    if (button == 3 && action == 1) {
        context_menu_show(&app->context_menu, x, y,
                          app->selected_count > 0,
                          app->clipboard_path[0] != '\0',
                          0);
        app->needs_redraw = 1;
        return;
    }
    
    /* Sidebar click */
    if (x < SIDEBAR_WIDTH && y >= content_y) {
        if (action == 1) {
            int idx = sidebar_hit_test(&app->sidebar, x, y, 0, content_y);
            if (idx >= 0) {
                const char *path = sidebar_on_click(&app->sidebar, idx);
                if (path) {
                    files_navigate(app, path);
                }
            }
        }
        return;
    }
    
    /* File view click */
    if (x >= SIDEBAR_WIDTH && y >= content_y && y < app->window_height - STATUS_BAR_HEIGHT) {
        if (action == 1) {
            /* Calculate which file was clicked based on view mode and position */
            int file_x = x - SIDEBAR_WIDTH;
            int file_y = y - content_y + app->scroll_y;
            
            int clicked_idx = -1;
            
            if (app->view_mode == VIEW_ICONS || app->view_mode == VIEW_GALLERY) {
                int cell_w = app->icon_size + 24;
                int cell_h = app->icon_size + 32;
                int cols = (app->window_width - SIDEBAR_WIDTH - 24) / cell_w;
                if (cols < 1) cols = 1;
                
                int col = file_x / cell_w;
                int row = file_y / cell_h;
                clicked_idx = row * cols + col;
            } else {
                /* List view */
                int row_height = 24;
                clicked_idx = file_y / row_height - 1;  /* -1 for header */
            }
            
            if (clicked_idx >= 0 && clicked_idx < app->entry_count) {
                /* Get ctrl modifier from last mouse event */
                /* In NXRender, ctrl is bit 1 of modifier flags */
                int ctrl = nx_get_modifiers() & 0x02;
                if (ctrl) {
                    files_toggle_selection(app, clicked_idx);
                } else {
                    files_select(app, clicked_idx);
                }
            } else {
                files_deselect_all(app);
            }
        }
    }
    
    app->needs_redraw = 1;
}
