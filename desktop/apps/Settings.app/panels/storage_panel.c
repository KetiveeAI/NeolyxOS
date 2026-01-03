/*
 * NeolyxOS Settings - Storage Panel
 * 
 * Disk storage management and analysis.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include <stdio.h>

/* Storage category */
typedef struct {
    const char* name;
    uint64_t bytes;
    rx_color color;
} storage_category_t;

/* Get storage info (would query actual disk in production) */
static void get_storage_info(uint64_t* total, uint64_t* used) {
    *total = 500ULL * 1024 * 1024 * 1024;  /* 500 GB */
    *used = 156ULL * 1024 * 1024 * 1024;   /* 156 GB used */
}

/* Format bytes to human readable */
static void format_bytes(uint64_t bytes, char* out, size_t len) {
    if (bytes >= 1024ULL * 1024 * 1024 * 1024) {
        snprintf(out, len, "%.1f TB", (double)bytes / (1024.0 * 1024 * 1024 * 1024));
    } else if (bytes >= 1024ULL * 1024 * 1024) {
        snprintf(out, len, "%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024ULL * 1024) {
        snprintf(out, len, "%.1f MB", (double)bytes / (1024.0 * 1024));
    } else {
        snprintf(out, len, "%lu KB", (unsigned long)(bytes / 1024));
    }
}

/* Create storage overview card */
static rx_view* create_storage_overview_card(void) {
    rx_view* card = settings_card("Storage Overview");
    if (!card) return NULL;
    
    uint64_t total, used;
    get_storage_info(&total, &used);
    uint64_t available = total - used;
    
    char total_str[32], used_str[32], avail_str[32];
    format_bytes(total, total_str, 32);
    format_bytes(used, used_str, 32);
    format_bytes(available, avail_str, 32);
    
    /* Main disk info */
    rx_text_view* disk_name = text_view_new("NeolyxOS SSD");
    if (disk_name) {
        text_view_set_font_size(disk_name, 16.0f);
        disk_name->font_weight = 500;
        disk_name->color = NX_COLOR_TEXT;
        view_add_child(card, (rx_view*)disk_name);
    }
    
    /* Usage summary */
    char usage_str[64];
    snprintf(usage_str, 64, "%s of %s used", used_str, total_str);
    rx_text_view* usage = settings_label(usage_str, true);
    if (usage) view_add_child(card, (rx_view*)usage);
    
    /* Available space */
    char avail_msg[64];
    snprintf(avail_msg, 64, "%s available", avail_str);
    rx_text_view* avail = text_view_new(avail_msg);
    if (avail) {
        avail->color = NX_COLOR_SUCCESS;
        view_add_child(card, (rx_view*)avail);
    }
    
    return card;
}

/* Create storage breakdown card */
static rx_view* create_breakdown_card(void) {
    rx_view* card = settings_card("Storage Breakdown");
    if (!card) return NULL;
    
    /* Categories */
    storage_category_t categories[] = {
        { "System", 45ULL * 1024 * 1024 * 1024, NX_COLOR_PRIMARY },
        { "Applications", 32ULL * 1024 * 1024 * 1024, NX_COLOR_SUCCESS },
        { "Documents", 28ULL * 1024 * 1024 * 1024, (rx_color){ 147, 51, 234, 255 } },
        { "Media", 35ULL * 1024 * 1024 * 1024, (rx_color){ 236, 72, 153, 255 } },
        { "Downloads", 12ULL * 1024 * 1024 * 1024, (rx_color){ 251, 191, 36, 255 } },
        { "Cache", 4ULL * 1024 * 1024 * 1024, (rx_color){ 156, 163, 175, 255 } },
    };
    int count = sizeof(categories) / sizeof(categories[0]);
    
    for (int i = 0; i < count; i++) {
        rx_view* row = hstack_new(12.0f);
        if (!row) continue;
        
        /* Color indicator */
        rx_view* indicator = vstack_new(0);
        if (indicator) {
            indicator->box.width = 12;
            indicator->box.height = 12;
            indicator->box.corner_radius = corners_all(3.0f);
            indicator->box.background = categories[i].color;
            view_add_child(row, indicator);
        }
        
        /* Name */
        rx_text_view* name = text_view_new(categories[i].name);
        if (name) {
            name->color = NX_COLOR_TEXT;
            view_add_child(row, (rx_view*)name);
        }
        
        /* Size */
        char size_str[32];
        format_bytes(categories[i].bytes, size_str, 32);
        rx_text_view* size = settings_label(size_str, true);
        if (size) view_add_child(row, (rx_view*)size);
        
        view_add_child(card, row);
    }
    
    return card;
}

/* Create disk management card */
static rx_view* create_disk_management_card(void) {
    rx_view* card = settings_card("Disk Management");
    if (!card) return NULL;
    
    /* Empty trash */
    rx_view* trash_row = hstack_new(8.0f);
    if (trash_row) {
        rx_text_view* label = text_view_new("Trash");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(trash_row, (rx_view*)label);
        }
        
        rx_text_view* size = settings_label("1.2 GB", true);
        if (size) view_add_child(trash_row, (rx_view*)size);
        
        rx_button_view* empty_btn = button_view_new("Empty");
        if (empty_btn) {
            empty_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
            view_add_child(trash_row, (rx_view*)empty_btn);
        }
        
        view_add_child(card, trash_row);
    }
    
    /* Clear cache button */
    rx_button_view* cache_btn = button_view_new("Clear System Cache");
    if (cache_btn) {
        cache_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        cache_btn->hover_color = (rx_color){ 80, 80, 82, 255 };
        view_add_child(card, (rx_view*)cache_btn);
    }
    
    /* Open Disk Utility */
    rx_button_view* nucleus_btn = button_view_new("Open NUCLEUS Disk Utility");
    if (nucleus_btn) {
        nucleus_btn->normal_color = NX_COLOR_PRIMARY;
        view_add_child(card, (rx_view*)nucleus_btn);
    }
    
    return card;
}

/* Main panel creation */
rx_view* storage_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Storage");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Overview card */
    rx_view* overview = create_storage_overview_card();
    if (overview) view_add_child(panel, overview);
    
    /* Breakdown card */
    rx_view* breakdown = create_breakdown_card();
    if (breakdown) view_add_child(panel, breakdown);
    
    /* Disk management */
    rx_view* management = create_disk_management_card();
    if (management) view_add_child(panel, management);
    
    println("[Storage] Panel created");
    return panel;
}
