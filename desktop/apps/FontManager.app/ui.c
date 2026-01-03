/*
 * NeolyxOS Font Manager - NXRender UI Implementation
 * 
 * Uses existing nxrender_c widgets for the Font Manager interface.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "ui.h"

/* ============ NXI Icon Widget ============ */

static void nxicon_render(nx_widget_t *self, nx_context_t *ctx);
static void nxicon_destroy(nx_widget_t *self);

static const nx_widget_vtable_t nxicon_vtable = {
    .render = nxicon_render,
    .layout = NULL,
    .measure = NULL,
    .handle_event = NULL,
    .destroy = nxicon_destroy
};

static void nxicon_render(nx_widget_t *self, nx_context_t *ctx) {
    nx_nxicon_widget_t *icon = (nx_nxicon_widget_t*)self;
    
    /* Get icon from system icons */
    const nxi_icon_t *nxi = nxi_get_icon(icon->icon_id);
    if (!nxi) return;
    
    /* Render using NXI renderer */
    nxi_render(nxi, self->bounds.x, self->bounds.y, icon->icon_size,
               (volatile uint32_t*)ctx->framebuffer, ctx->pitch,
               ctx->width, ctx->height);
}

static void nxicon_destroy(nx_widget_t *self) {
    /* No dynamic memory in this widget */
    (void)self;
}

nx_nxicon_widget_t* nx_nxicon_create(uint32_t icon_id, uint32_t size) {
    nx_nxicon_widget_t *w = (nx_nxicon_widget_t*)kmalloc(sizeof(nx_nxicon_widget_t));
    if (!w) return NULL;
    
    nx_widget_init(&w->base, &nxicon_vtable);
    w->icon_id = icon_id;
    w->icon_size = size;
    w->tint_color = 0xFFFFFFFF;
    w->base.bounds.width = size;
    w->base.bounds.height = size;
    
    return w;
}

void nx_nxicon_set_tint(nx_nxicon_widget_t *w, uint32_t color) {
    if (w) w->tint_color = color;
}

/* ============ Event Handlers ============ */

static void on_filter_all_click(nx_widget_t *btn, void *ctx) {
    fontmanager_ui_t *ui = (fontmanager_ui_t*)ctx;
    fm_action_t action = {.type = FM_ACTION_FILTER};
    action.payload.filter.filter = FM_FILTER_ALL;
    fm_dispatch(ui->state, &action);
    fontmanager_ui_update(ui);
}

static void on_filter_installed_click(nx_widget_t *btn, void *ctx) {
    fontmanager_ui_t *ui = (fontmanager_ui_t*)ctx;
    fm_action_t action = {.type = FM_ACTION_FILTER};
    action.payload.filter.filter = FM_FILTER_INSTALLED;
    fm_dispatch(ui->state, &action);
    fontmanager_ui_update(ui);
}

static void on_install_click(nx_widget_t *btn, void *ctx) {
    fontmanager_ui_t *ui = (fontmanager_ui_t*)ctx;
    const fm_font_entry_t *font = fm_select_current_font(ui->state);
    if (font && !font->system_font) {
        /* Toggle install state */
        fm_action_t action = {.type = font->installed ? FM_ACTION_UNINSTALL_FONT : FM_ACTION_INSTALL_FONT};
        action.payload.install.font_id = font->id;
        fm_dispatch(ui->state, &action);
        fontmanager_ui_update(ui);
    }
}

/* ============ UI Building ============ */

int fontmanager_ui_init(fontmanager_ui_t *ui, nx_context_t *ctx, fm_state_t *state) {
    if (!ui || !ctx || !state) return -1;
    
    ui->ctx = ctx;
    ui->state = state;
    
    /* Initialize NXI icons */
    nxi_init();
    
    return 0;
}

void fontmanager_ui_build(fontmanager_ui_t *ui) {
    if (!ui || !ui->ctx) return;
    
    /* Create main window */
    nx_rect_t win_rect = {
        .x = ui->state->window_x,
        .y = ui->state->window_y, 
        .width = ui->state->window_w,
        .height = ui->state->window_h
    };
    ui->window = nx_window_create("Font Manager", win_rect, 0);
    
    /* Create sidebar container (180px wide) */
    ui->sidebar = nx_container_create();
    nx_rect_t sidebar_bounds = {0, 0, 180, ui->state->window_h - 28};
    nx_widget_layout(ui->sidebar, sidebar_bounds);
    
    /* Add filter buttons to sidebar */
    const char *filter_labels[] = {"All Fonts", "Installed", "System", "User"};
    for (int i = 0; i < 4; i++) {
        ui->filter_buttons[i] = (nx_widget_t*)nx_button_create(filter_labels[i]);
        nx_widget_add_child(ui->sidebar, ui->filter_buttons[i]);
    }
    
    /* Create font list panel */
    ui->font_list = nx_container_create();
    
    /* Create preview panel */
    ui->preview_panel = nx_container_create();
    
    /* Add preview components */
    ui->preview_label = (nx_widget_t*)nx_label_create("Select a font");
    nx_widget_add_child(ui->preview_panel, ui->preview_label);
    
    ui->install_button = (nx_widget_t*)nx_button_create("Install");
    nx_widget_add_child(ui->preview_panel, ui->install_button);
    
    /* Add icon widgets */
    ui->font_icon = (nx_widget_t*)nx_nxicon_create(NXI_ICON_FONT, 32);
    ui->folder_icon = (nx_widget_t*)nx_nxicon_create(NXI_ICON_FOLDER, 24);
    
    /* Set window root */
    nx_widget_t *root = nx_container_create();
    nx_widget_add_child(root, ui->sidebar);
    nx_widget_add_child(root, ui->font_list);
    nx_widget_add_child(root, ui->preview_panel);
    nx_window_set_root_widget(ui->window, root);
}

void fontmanager_ui_update(fontmanager_ui_t *ui) {
    if (!ui) return;
    
    /* Update preview label with selected font */
    const fm_font_entry_t *font = fm_select_current_font(ui->state);
    if (font && ui->preview_label) {
        /* Cast to label and update text */
        nx_label_t *label = (nx_label_t*)ui->preview_label;
        nx_label_set_text(label, font->name);
    }
    
    /* Update install button text */
    if (font && ui->install_button) {
        nx_button_t *btn = (nx_button_t*)ui->install_button;
        nx_button_set_label(btn, font->installed ? "Uninstall" : "Install");
    }
}

void fontmanager_ui_render(fontmanager_ui_t *ui) {
    if (!ui || !ui->window) return;
    
    nx_window_render(ui->window, ui->ctx);
}

int fontmanager_ui_handle_event(fontmanager_ui_t *ui, nx_event_t *event) {
    if (!ui || !ui->window || !event) return 0;
    
    return nx_window_handle_event(ui->window, event);
}

void fontmanager_ui_destroy(fontmanager_ui_t *ui) {
    if (!ui) return;
    
    if (ui->window) {
        nx_window_destroy(ui->window);
        ui->window = NULL;
    }
}
