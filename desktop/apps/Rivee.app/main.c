/*
 * Rivee - Draw your imagination
 * Professional Vector Graphics Editor for NeolyxOS
 * 
 * Main application entry point.
 * Uses NXRender for graphics and ReoxUI for interface.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "include/rivee.h"

/* ReoxUI forward declarations */
typedef struct reox_window reox_window_t;
typedef struct reox_widget reox_widget_t;

extern reox_window_t *reox_window_create(const char *title, int width, int height);
extern void reox_window_show(reox_window_t *win);
extern void reox_window_set_menubar(reox_window_t *win, reox_widget_t *menubar);
extern void reox_window_set_content(reox_window_t *win, reox_widget_t *content);
extern int reox_app_run(void);
extern void reox_app_quit(void);

/* Widget creation */
extern reox_widget_t *reox_hbox_new(int spacing);
extern reox_widget_t *reox_vbox_new(int spacing);
extern reox_widget_t *reox_button_new(const char *label);
extern reox_widget_t *reox_canvas_new(int width, int height);
extern reox_widget_t *reox_panel_new(const char *title);
extern reox_widget_t *reox_menubar_new(void);
extern reox_widget_t *reox_menu_new(const char *label);
extern reox_widget_t *reox_menuitem_new(const char *label, void (*callback)(void));

extern void reox_container_add(reox_widget_t *container, reox_widget_t *child);
extern void reox_menu_add_item(reox_widget_t *menu, reox_widget_t *item);
extern void reox_menubar_add_menu(reox_widget_t *bar, reox_widget_t *menu);

/* NXRender forward declarations */
extern void *nxrender_surface_from_widget(reox_widget_t *canvas);
extern void nxrender_clear(void *surface, rivee_color_t color);
extern void nxrender_present(void *surface);

/* ============ Application State ============ */

typedef struct {
    reox_window_t *window;
    reox_widget_t *canvas;
    reox_widget_t *toolbar;
    reox_widget_t *layers_panel;
    reox_widget_t *properties_panel;
    reox_widget_t *color_picker;
    
    rivee_document_t *document;
    rivee_tool_t current_tool;
    rivee_color_t fill_color;
    rivee_color_t stroke_color;
    float stroke_width;
    
    bool is_drawing;
    rivee_point_t draw_start;
    rivee_shape_t *temp_shape;
} rivee_app_t;

static rivee_app_t app = {0};

/* ============ Tool Handlers ============ */

static void on_canvas_mouse_down(float x, float y) {
    app.is_drawing = true;
    app.draw_start = (rivee_point_t){x, y};
    
    switch (app.current_tool) {
        case TOOL_RECTANGLE:
            app.temp_shape = rivee_shape_create_rect(x, y, 0, 0);
            break;
        case TOOL_ELLIPSE:
            app.temp_shape = rivee_shape_create_ellipse(x, y, 0, 0);
            break;
        case TOOL_LINE:
            app.temp_shape = rivee_shape_create_line(x, y, x, y);
            break;
        case TOOL_PEN:
            app.temp_shape = rivee_shape_create_path();
            rivee_path_move_to(&app.temp_shape->data.path, x, y);
            break;
        default:
            break;
    }
    
    if (app.temp_shape) {
        rivee_shape_set_fill_color(app.temp_shape, app.fill_color);
        rivee_shape_set_stroke(app.temp_shape, app.stroke_color, app.stroke_width);
    }
}

static void on_canvas_mouse_move(float x, float y) {
    if (!app.is_drawing || !app.temp_shape) return;
    
    float dx = x - app.draw_start.x;
    float dy = y - app.draw_start.y;
    
    switch (app.current_tool) {
        case TOOL_RECTANGLE:
            app.temp_shape->data.rect.width = dx;
            app.temp_shape->data.rect.height = dy;
            break;
        case TOOL_ELLIPSE:
            app.temp_shape->data.ellipse.rx = dx / 2;
            app.temp_shape->data.ellipse.ry = dy / 2;
            app.temp_shape->data.ellipse.center.x = app.draw_start.x + dx / 2;
            app.temp_shape->data.ellipse.center.y = app.draw_start.y + dy / 2;
            break;
        case TOOL_LINE:
            app.temp_shape->data.line.end.x = x;
            app.temp_shape->data.line.end.y = y;
            break;
        case TOOL_PEN:
            rivee_path_line_to(&app.temp_shape->data.path, x, y);
            break;
        default:
            break;
    }
    
    /* Redraw canvas */
    void *surface = nxrender_surface_from_widget(app.canvas);
    rivee_render_document(app.document, surface);
    if (app.temp_shape) {
        rivee_render_shape(app.temp_shape, surface);
    }
    nxrender_present(surface);
}

static void on_canvas_mouse_up(float x, float y) {
    (void)x; (void)y;
    
    if (!app.is_drawing) return;
    app.is_drawing = false;
    
    if (app.temp_shape && app.document->active_layer) {
        rivee_shape_add(app.document->active_layer, app.temp_shape);
        app.document->modified = true;
    }
    
    app.temp_shape = NULL;
    
    /* Redraw */
    void *surface = nxrender_surface_from_widget(app.canvas);
    rivee_render_document(app.document, surface);
    nxrender_present(surface);
}

/* ============ Tool Selection ============ */

static void select_tool(rivee_tool_t tool) {
    app.current_tool = tool;
}

static void on_tool_select(void) { select_tool(TOOL_SELECT); }
static void on_tool_rect(void) { select_tool(TOOL_RECTANGLE); }
static void on_tool_ellipse(void) { select_tool(TOOL_ELLIPSE); }
static void on_tool_line(void) { select_tool(TOOL_LINE); }
static void on_tool_pen(void) { select_tool(TOOL_PEN); }
static void on_tool_text(void) { select_tool(TOOL_TEXT); }

/* ============ Menu Callbacks ============ */

static void on_file_new(void) {
    if (app.document) {
        rivee_document_free(app.document);
    }
    app.document = rivee_document_new(800, 600);
    rivee_layer_new(app.document, "Layer 1");
}

static void on_file_open(void) {
    /* TODO: File dialog */
}

static void on_file_save(void) {
    if (app.document && app.document->path[0]) {
        rivee_document_save(app.document, app.document->path);
    }
}

static void on_file_export_png(void) {
    if (app.document) {
        rivee_export_png(app.document, "export.png", 1);
    }
}

static void on_file_export_nxi(void) {
    if (app.document) {
        rivee_export_nxi(app.document, "export.nxi");
    }
}

static void on_file_quit(void) {
    reox_app_quit();
}

static void on_edit_undo(void) {
    /* TODO: Undo system */
}

static void on_edit_redo(void) {
    /* TODO: Redo system */
}

static void on_edit_select_all(void) {
    if (app.document) {
        rivee_select_all(app.document);
    }
}

/* ============ UI Creation ============ */

static reox_widget_t *create_menubar(void) {
    reox_widget_t *menubar = reox_menubar_new();
    
    /* File menu */
    reox_widget_t *file_menu = reox_menu_new("File");
    reox_menu_add_item(file_menu, reox_menuitem_new("New", on_file_new));
    reox_menu_add_item(file_menu, reox_menuitem_new("Open...", on_file_open));
    reox_menu_add_item(file_menu, reox_menuitem_new("Save", on_file_save));
    reox_menu_add_item(file_menu, reox_menuitem_new("Export PNG...", on_file_export_png));
    reox_menu_add_item(file_menu, reox_menuitem_new("Export NXI...", on_file_export_nxi));
    reox_menu_add_item(file_menu, reox_menuitem_new("Quit", on_file_quit));
    reox_menubar_add_menu(menubar, file_menu);
    
    /* Edit menu */
    reox_widget_t *edit_menu = reox_menu_new("Edit");
    reox_menu_add_item(edit_menu, reox_menuitem_new("Undo", on_edit_undo));
    reox_menu_add_item(edit_menu, reox_menuitem_new("Redo", on_edit_redo));
    reox_menu_add_item(edit_menu, reox_menuitem_new("Select All", on_edit_select_all));
    reox_menubar_add_menu(menubar, edit_menu);
    
    return menubar;
}

static reox_widget_t *create_toolbar(void) {
    reox_widget_t *toolbar = reox_vbox_new(4);
    
    reox_widget_t *btn_select = reox_button_new("Select");
    reox_widget_t *btn_rect = reox_button_new("Rect");
    reox_widget_t *btn_ellipse = reox_button_new("Ellipse");
    reox_widget_t *btn_line = reox_button_new("Line");
    reox_widget_t *btn_pen = reox_button_new("Pen");
    reox_widget_t *btn_text = reox_button_new("Text");
    
    /* TODO: Connect button signals */
    (void)btn_select; (void)btn_rect; (void)btn_ellipse;
    (void)btn_line; (void)btn_pen; (void)btn_text;
    
    reox_container_add(toolbar, btn_select);
    reox_container_add(toolbar, btn_rect);
    reox_container_add(toolbar, btn_ellipse);
    reox_container_add(toolbar, btn_line);
    reox_container_add(toolbar, btn_pen);
    reox_container_add(toolbar, btn_text);
    
    return toolbar;
}

static reox_widget_t *create_layers_panel(void) {
    reox_widget_t *panel = reox_panel_new("Layers");
    
    /* TODO: Layer list widget */
    
    return panel;
}

static reox_widget_t *create_properties_panel(void) {
    reox_widget_t *panel = reox_panel_new("Properties");
    
    /* TODO: Property editors */
    
    return panel;
}

/* ============ Main Entry ============ */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    /* Initialize application state */
    app.current_tool = TOOL_SELECT;
    app.fill_color = RIVEE_COLOR_WHITE;
    app.stroke_color = RIVEE_COLOR_BLACK;
    app.stroke_width = 2.0f;
    
    /* Create new document */
    app.document = rivee_document_new(800, 600);
    rivee_layer_new(app.document, "Layer 1");
    
    /* Create main window */
    app.window = reox_window_create("Rivee - Draw your imagination", 1200, 800);
    
    /* Create menu bar */
    reox_widget_t *menubar = create_menubar();
    reox_window_set_menubar(app.window, menubar);
    
    /* Create main layout */
    reox_widget_t *main_hbox = reox_hbox_new(0);
    
    /* Left toolbar */
    app.toolbar = create_toolbar();
    reox_container_add(main_hbox, app.toolbar);
    
    /* Center canvas */
    app.canvas = reox_canvas_new(800, 600);
    reox_container_add(main_hbox, app.canvas);
    
    /* Right panels */
    reox_widget_t *right_vbox = reox_vbox_new(8);
    app.layers_panel = create_layers_panel();
    app.properties_panel = create_properties_panel();
    reox_container_add(right_vbox, app.layers_panel);
    reox_container_add(right_vbox, app.properties_panel);
    reox_container_add(main_hbox, right_vbox);
    
    reox_window_set_content(app.window, main_hbox);
    
    /* Show window */
    reox_window_show(app.window);
    
    /* Run application */
    return reox_app_run();
}
