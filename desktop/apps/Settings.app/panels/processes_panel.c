/*
 * NeolyxOS Settings - Process Manager Panel
 * 
 * View and manage running processes
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <stdio.h>
#include <string.h>

/* Process list */
static process_info_t g_processes[100];
static int g_process_count = 0;

/* Performance stats */
static cpu_info_t g_cpu;
static memory_info_t g_memory;

/* Load process list */
static void load_processes(void) {
    process_list(g_processes, 100, &g_process_count);
    perf_get_cpu_info(&g_cpu);
    perf_get_memory_info(&g_memory);
}

/* Create system overview card */
static rx_view* create_overview_card(void) {
    rx_view* card = settings_card("System Overview");
    if (!card) return NULL;
    
    /* CPU usage */
    char cpu_str[64];
    snprintf(cpu_str, 64, "%.1f%% (%.0f°C)", g_cpu.cpu_usage, g_cpu.cpu_temp);
    rx_view* cpu_row = settings_row("CPU Usage", cpu_str);
    if (cpu_row) view_add_child(card, cpu_row);
    
    /* CPU bar */
    rx_view* cpu_bar_bg = hstack_new(0);
    if (cpu_bar_bg) {
        cpu_bar_bg->box.background = (rx_color){ 60, 60, 62, 255 };
        cpu_bar_bg->box.height = 8;
        cpu_bar_bg->box.corner_radius = corners_all(4.0f);
        
        rx_view* cpu_bar_fill = hstack_new(0);
        if (cpu_bar_fill) {
            cpu_bar_fill->box.background = g_cpu.cpu_usage > 80 ? NX_COLOR_ERROR : NX_COLOR_PRIMARY;
            cpu_bar_fill->box.width = (int)(g_cpu.cpu_usage * 2);  /* scale */
            cpu_bar_fill->box.height = 8;
            cpu_bar_fill->box.corner_radius = corners_all(4.0f);
            view_add_child(cpu_bar_bg, cpu_bar_fill);
        }
        view_add_child(card, cpu_bar_bg);
    }
    
    /* Memory usage */
    float mem_percent = (float)g_memory.used_mb / g_memory.total_mb * 100;
    char mem_str[64];
    snprintf(mem_str, 64, "%lu MB / %lu MB (%.1f%%)", 
             (unsigned long)g_memory.used_mb, 
             (unsigned long)g_memory.total_mb, 
             mem_percent);
    rx_view* mem_row = settings_row("Memory", mem_str);
    if (mem_row) view_add_child(card, mem_row);
    
    /* Memory bar */
    rx_view* mem_bar_bg = hstack_new(0);
    if (mem_bar_bg) {
        mem_bar_bg->box.background = (rx_color){ 60, 60, 62, 255 };
        mem_bar_bg->box.height = 8;
        mem_bar_bg->box.corner_radius = corners_all(4.0f);
        
        rx_view* mem_bar_fill = hstack_new(0);
        if (mem_bar_fill) {
            mem_bar_fill->box.background = mem_percent > 80 ? NX_COLOR_ERROR : NX_COLOR_SUCCESS;
            mem_bar_fill->box.width = (int)(mem_percent * 2);
            mem_bar_fill->box.height = 8;
            mem_bar_fill->box.corner_radius = corners_all(4.0f);
            view_add_child(mem_bar_bg, mem_bar_fill);
        }
        view_add_child(card, mem_bar_bg);
    }
    
    /* Process count */
    char proc_str[16];
    snprintf(proc_str, 16, "%d", g_process_count);
    rx_view* proc_row = settings_row("Running Processes", proc_str);
    if (proc_row) view_add_child(card, proc_row);
    
    return card;
}

/* Create process list card */
static rx_view* create_process_list_card(void) {
    rx_view* card = settings_card("Processes");
    if (!card) return NULL;
    
    /* Header row */
    rx_view* header = hstack_new(8.0f);
    if (header) {
        rx_text_view* name_hdr = settings_label("Name", false);
        if (name_hdr) {
            name_hdr->font_weight = 600;
            view_add_child(header, (rx_view*)name_hdr);
        }
        
        rx_text_view* cpu_hdr = settings_label("CPU", false);
        if (cpu_hdr) {
            cpu_hdr->font_weight = 600;
            view_add_child(header, (rx_view*)cpu_hdr);
        }
        
        rx_text_view* mem_hdr = settings_label("Memory", false);
        if (mem_hdr) {
            mem_hdr->font_weight = 600;
            view_add_child(header, (rx_view*)mem_hdr);
        }
        
        view_add_child(card, header);
    }
    
    /* Divider */
    rx_view* divider = hstack_new(0);
    if (divider) {
        divider->box.background = (rx_color){ 60, 60, 62, 255 };
        divider->box.height = 1;
        view_add_child(card, divider);
    }
    
    /* Process rows (show top 10 by CPU) */
    int show_count = g_process_count > 10 ? 10 : g_process_count;
    for (int i = 0; i < show_count; i++) {
        rx_view* row = hstack_new(8.0f);
        if (row) {
            /* Name */
            rx_text_view* name = text_view_new(g_processes[i].name);
            if (name) {
                name->color = NX_COLOR_TEXT;
                view_add_child(row, (rx_view*)name);
            }
            
            /* CPU */
            char cpu_str[16];
            snprintf(cpu_str, 16, "%.1f%%", g_processes[i].cpu_percent);
            rx_text_view* cpu = settings_label(cpu_str, true);
            if (cpu) {
                if (g_processes[i].cpu_percent > 50) cpu->color = NX_COLOR_ERROR;
                view_add_child(row, (rx_view*)cpu);
            }
            
            /* Memory */
            char mem_str[32];
            if (g_processes[i].memory_kb >= 1024 * 1024) {
                snprintf(mem_str, 32, "%.1f GB", g_processes[i].memory_kb / 1024.0 / 1024.0);
            } else if (g_processes[i].memory_kb >= 1024) {
                snprintf(mem_str, 32, "%.1f MB", g_processes[i].memory_kb / 1024.0);
            } else {
                snprintf(mem_str, 32, "%lu KB", (unsigned long)g_processes[i].memory_kb);
            }
            rx_text_view* mem = settings_label(mem_str, true);
            if (mem) view_add_child(row, (rx_view*)mem);
            
            view_add_child(card, row);
        }
    }
    
    /* Show all button */
    if (g_process_count > 10) {
        char more_str[32];
        snprintf(more_str, 32, "Show All (%d more)", g_process_count - 10);
        rx_button_view* more_btn = button_view_new(more_str);
        if (more_btn) {
            more_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
            view_add_child(card, (rx_view*)more_btn);
        }
    }
    
    return card;
}

/* Create actions card */
static rx_view* create_actions_card(void) {
    rx_view* card = settings_card("Actions");
    if (!card) return NULL;
    
    rx_view* btns = hstack_new(12.0f);
    if (btns) {
        rx_button_view* refresh_btn = button_view_new("Refresh");
        if (refresh_btn) {
            refresh_btn->normal_color = NX_COLOR_PRIMARY;
            view_add_child(btns, (rx_view*)refresh_btn);
        }
        
        rx_button_view* kill_btn = button_view_new("End Process");
        if (kill_btn) {
            kill_btn->normal_color = NX_COLOR_ERROR;
            view_add_child(btns, (rx_view*)kill_btn);
        }
        
        view_add_child(card, btns);
    }
    
    return card;
}

/* Main panel creation */
rx_view* processes_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    load_processes();
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Process Manager");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Overview card */
    rx_view* overview_card = create_overview_card();
    if (overview_card) view_add_child(panel, overview_card);
    
    /* Process list card */
    rx_view* list_card = create_process_list_card();
    if (list_card) view_add_child(panel, list_card);
    
    /* Actions card */
    rx_view* actions_card = create_actions_card();
    if (actions_card) view_add_child(panel, actions_card);
    
    println("[Processes] Panel created");
    return panel;
}
