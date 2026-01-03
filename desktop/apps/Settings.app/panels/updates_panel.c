/*
 * NeolyxOS Settings - Updates Panel
 * 
 * System and application updates management.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include <stdio.h>
#include <string.h>

/* Check for system updates - queries update service */
static void check_system_updates(settings_ctx_t* ctx) {
    ctx->system_update.state = UPDATE_STATE_CHECKING;
    println("[Updates] Checking for system updates...");
    
    /* Get current version from kernel */
    extern const char* kernel_version;  /* From kernel info */
    strncpy(ctx->system_update.current_version, "1.0.0", 32);  /* Default */
    
    /* Query update service for available updates */
    /* Would call: update_service_check(&ctx->system_update); */
    
    /* If no update service response, state stays IDLE after check */
    ctx->system_update.state = UPDATE_STATE_IDLE;
    ctx->system_update.download_size = 0;
    ctx->system_update.downloaded = 0;
    ctx->system_update.progress = 0;
}

/* Check for app updates */
static void check_app_updates(settings_ctx_t* ctx) {
    println("[Updates] Checking for application updates...");
    ctx->app_update_count = 0;
    /* In production, query app store for updates */
}

/* Create system update card */
static rx_view* create_system_update_card(settings_ctx_t* ctx) {
    rx_view* card = settings_card("System Update");
    if (!card) return NULL;
    
    /* Current version row */
    rx_view* current_row = settings_row("Current Version", ctx->system_update.current_version);
    if (current_row) view_add_child(card, current_row);
    
    /* Update status based on state */
    switch (ctx->system_update.state) {
        case UPDATE_STATE_IDLE: {
            rx_text_view* status = settings_label("No updates checked", true);
            if (status) view_add_child(card, (rx_view*)status);
            
            rx_button_view* check_btn = button_view_new("Check for Updates");
            if (check_btn) {
                check_btn->normal_color = NX_COLOR_PRIMARY;
                check_btn->hover_color = (rx_color){ 30, 144, 255, 255 };
                check_btn->pressed_color = (rx_color){ 0, 100, 220, 255 };
                view_add_child(card, (rx_view*)check_btn);
            }
            break;
        }
        
        case UPDATE_STATE_CHECKING: {
            rx_text_view* status = settings_label("Checking for updates...", true);
            if (status) view_add_child(card, (rx_view*)status);
            break;
        }
        
        case UPDATE_STATE_AVAILABLE: {
            /* Show available update */
            rx_view* avail_row = settings_row("Available Update", ctx->system_update.version);
            if (avail_row) view_add_child(card, avail_row);
            
            /* Download size */
            char size_str[32];
            uint64_t mb = ctx->system_update.download_size / (1024 * 1024);
            snprintf(size_str, 32, "%lu MB", (unsigned long)mb);
            rx_view* size_row = settings_row("Download Size", size_str);
            if (size_row) view_add_child(card, size_row);
            
            /* Release notes section */
            rx_view* notes_section = settings_section("Release Notes");
            if (notes_section) view_add_child(card, notes_section);
            
            rx_text_view* notes = text_view_new(ctx->system_update.release_notes);
            if (notes) {
                notes->color = NX_COLOR_TEXT_DIM;
                text_view_set_font_size(notes, 13.0f);
                view_add_child(card, (rx_view*)notes);
            }
            
            /* Download button */
            rx_button_view* download_btn = button_view_new("Download and Install");
            if (download_btn) {
                download_btn->normal_color = NX_COLOR_SUCCESS;
                download_btn->hover_color = (rx_color){ 54, 217, 114, 255 };
                download_btn->pressed_color = (rx_color){ 24, 177, 74, 255 };
                view_add_child(card, (rx_view*)download_btn);
            }
            break;
        }
        
        case UPDATE_STATE_DOWNLOADING: {
            /* Progress indicator */
            char progress_str[32];
            snprintf(progress_str, 32, "Downloading... %d%%", ctx->system_update.progress);
            rx_text_view* progress = settings_label(progress_str, false);
            if (progress) {
                progress->color = NX_COLOR_PRIMARY;
                view_add_child(card, (rx_view*)progress);
            }
            break;
        }
        
        case UPDATE_STATE_READY: {
            rx_text_view* status = settings_label("Update ready to install", false);
            if (status) {
                status->color = NX_COLOR_SUCCESS;
                view_add_child(card, (rx_view*)status);
            }
            
            rx_button_view* install_btn = button_view_new("Install Now");
            if (install_btn) {
                install_btn->normal_color = NX_COLOR_SUCCESS;
                view_add_child(card, (rx_view*)install_btn);
            }
            
            rx_button_view* later_btn = button_view_new("Install Later");
            if (later_btn) {
                later_btn->normal_color = NX_COLOR_SURFACE;
                view_add_child(card, (rx_view*)later_btn);
            }
            break;
        }
        
        case UPDATE_STATE_ERROR: {
            rx_text_view* error = settings_label(ctx->system_update.error, false);
            if (error) {
                error->color = NX_COLOR_ERROR;
                view_add_child(card, (rx_view*)error);
            }
            
            rx_button_view* retry_btn = button_view_new("Retry");
            if (retry_btn) {
                retry_btn->normal_color = NX_COLOR_PRIMARY;
                view_add_child(card, (rx_view*)retry_btn);
            }
            break;
        }
        
        default:
            break;
    }
    
    return card;
}

/* Create app updates card */
static rx_view* create_app_updates_card(settings_ctx_t* ctx) {
    rx_view* card = settings_card("Application Updates");
    if (!card) return NULL;
    
    if (ctx->app_update_count == 0) {
        rx_text_view* status = settings_label("All applications are up to date", true);
        if (status) view_add_child(card, (rx_view*)status);
    } else {
        /* List app updates */
        app_update_t* update = ctx->app_updates;
        while (update) {
            rx_view* app_row = hstack_new(12.0f);
            if (app_row) {
                rx_text_view* name = text_view_new(update->name);
                if (name) view_add_child(app_row, (rx_view*)name);
                
                char ver_str[64];
                snprintf(ver_str, 64, "%s -> %s", update->current, update->available);
                rx_text_view* ver = settings_label(ver_str, true);
                if (ver) view_add_child(app_row, (rx_view*)ver);
                
                view_add_child(card, app_row);
            }
            update = update->next;
        }
        
        rx_button_view* update_all = button_view_new("Update All");
        if (update_all) {
            update_all->normal_color = NX_COLOR_PRIMARY;
            view_add_child(card, (rx_view*)update_all);
        }
    }
    
    return card;
}

/* Create auto-update settings card */
static rx_view* create_auto_update_card(void) {
    rx_view* card = settings_card("Automatic Updates");
    if (!card) return NULL;
    
    /* Auto-check toggle */
    rx_view* auto_check_row = hstack_new(8.0f);
    if (auto_check_row) {
        rx_text_view* label = text_view_new("Check for updates automatically");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(auto_check_row, (rx_view*)label);
        }
        /* Toggle would go here */
        view_add_child(card, auto_check_row);
    }
    
    /* Auto-download toggle */
    rx_view* auto_download_row = hstack_new(8.0f);
    if (auto_download_row) {
        rx_text_view* label = text_view_new("Download updates automatically");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(auto_download_row, (rx_view*)label);
        }
        view_add_child(card, auto_download_row);
    }
    
    /* Schedule settings */
    rx_text_view* schedule = settings_label("Updates will be installed during idle time", true);
    if (schedule) view_add_child(card, (rx_view*)schedule);
    
    return card;
}

/* Main panel creation */
rx_view* updates_panel_create(settings_ctx_t* ctx) {
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Software Update");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Initialize update state if needed */
    if (ctx->system_update.state == UPDATE_STATE_IDLE) {
        strncpy(ctx->system_update.current_version, "1.0.0", 32);
    }
    
    /* System update card */
    rx_view* sys_card = create_system_update_card(ctx);
    if (sys_card) view_add_child(panel, sys_card);
    
    /* App updates card */
    rx_view* app_card = create_app_updates_card(ctx);
    if (app_card) view_add_child(panel, app_card);
    
    /* Auto-update settings */
    rx_view* auto_card = create_auto_update_card();
    if (auto_card) view_add_child(panel, auto_card);
    
    println("[Updates] Panel created");
    return panel;
}
