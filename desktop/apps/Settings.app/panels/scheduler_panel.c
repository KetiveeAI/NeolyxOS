/*
 * NeolyxOS Settings - Task Scheduler Panel
 * 
 * View and manage cron jobs and scheduled tasks
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <stdio.h>

typedef struct {
    char name[64];
    char schedule[32];
    char next_run[32];
    bool enabled;
} scheduled_task_t;

static scheduled_task_t g_tasks[] = {
    { "System Backup", "Daily @ 03:00", "Tomorrow 03:00", true },
    { "Check Updates", "Every 6 hours", "Today 14:00", true },
    { "Clean Temp Files", "Weekly (Sun)", "Sunday 00:00", true },
    { "Disk Defrag", "Monthly", "Feb 1 02:00", false }
};

static rx_view* create_tasks_card(void) {
    rx_view* card = settings_card("Scheduled Tasks");
    if (!card) return NULL;
    
    int count = sizeof(g_tasks) / sizeof(g_tasks[0]);
    for (int i = 0; i < count; i++) {
        rx_view* row = hstack_new(12.0f);
        
        /* Info */
        rx_view* info = vstack_new(2.0f);
        info->box.width = 250;
        
        rx_text_view* name = text_view_new(g_tasks[i].name);
        name->color = NX_COLOR_TEXT;
        view_add_child(info, (rx_view*)name);
        
        rx_text_view* sched = settings_label(g_tasks[i].schedule, true);
        view_add_child(info, (rx_view*)sched);
        
        view_add_child(row, info);
        
        /* Status button */
        rx_button_view* btn = button_view_new(g_tasks[i].enabled ? "Active" : "Paused");
        btn->normal_color = g_tasks[i].enabled ? NX_COLOR_SUCCESS : (rx_color){ 60, 60, 62, 255 };
        view_add_child(row, (rx_view*)btn);
        
        view_add_child(card, row);
    }
    
    rx_button_view* add_btn = button_view_new("Create New Task");
    add_btn->normal_color = NX_COLOR_PRIMARY;
    view_add_child(card, (rx_view*)add_btn);
    
    return card;
}

rx_view* scheduler_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    rx_text_view* title = text_view_new("Task Scheduler");
    text_view_set_font_size(title, 28.0f);
    title->font_weight = 700;
    title->color = NX_COLOR_TEXT;
    view_add_child(panel, (rx_view*)title);
    
    view_add_child(panel, create_tasks_card());
    
    println("[Scheduler] Panel created");
    return panel;
}
