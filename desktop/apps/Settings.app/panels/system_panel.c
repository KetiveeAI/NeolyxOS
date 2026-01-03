/*
 * NeolyxOS Settings - System Panel
 * 
 * System information: OS version, hardware, uptime.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "panel_common.h"

rx_view* system_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(SECTION_GAP);
    if (!panel) return NULL;
    
    /* Header */
    rx_text_view* header = panel_header("System Information");
    if (header) view_add_child(panel, (rx_view*)header);
    
    /* About Card */
    rx_view* about_card = settings_card("About NeolyxOS");
    if (about_card) {
        rx_text_view* os_name = text_view_new("NeolyxOS");
        if (os_name) {
            text_view_set_font_size(os_name, 24.0f);
            os_name->font_weight = 700;
            os_name->color = NX_COLOR_PRIMARY;
            view_add_child(about_card, (rx_view*)os_name);
        }
        
        rx_view* ver_row = hstack_new(8.0f);
        if (ver_row) {
            rx_text_view* ver_label = settings_label("Version:", true);
            rx_text_view* ver_value = text_view_new("1.0.0-alpha1");
            if (ver_label) view_add_child(ver_row, (rx_view*)ver_label);
            if (ver_value) {
                ver_value->color = NX_COLOR_TEXT;
                view_add_child(ver_row, (rx_view*)ver_value);
            }
            view_add_child(about_card, ver_row);
        }
        
        rx_view* build_row = hstack_new(8.0f);
        if (build_row) {
            rx_text_view* build_label = settings_label("Build:", true);
            rx_text_view* build_value = text_view_new("2025.12.23");
            if (build_label) view_add_child(build_row, (rx_view*)build_label);
            if (build_value) {
                build_value->color = NX_COLOR_TEXT;
                view_add_child(build_row, (rx_view*)build_value);
            }
            view_add_child(about_card, build_row);
        }
        view_add_child(panel, about_card);
    }
    
    /* Hardware Card */
    rx_view* hw_card = settings_card("Hardware");
    if (hw_card) {
        /* CPU */
        rx_view* cpu_row = hstack_new(8.0f);
        if (cpu_row) {
            rx_text_view* cpu_label = settings_label("CPU:", true);
            rx_text_view* cpu_value = text_view_new("Intel Core (via CPUID)");
            if (cpu_label) view_add_child(cpu_row, (rx_view*)cpu_label);
            if (cpu_value) {
                cpu_value->color = NX_COLOR_TEXT;
                view_add_child(cpu_row, (rx_view*)cpu_value);
            }
            view_add_child(hw_card, cpu_row);
        }
        
        /* Memory */
        rx_view* mem_row = hstack_new(8.0f);
        if (mem_row) {
            rx_text_view* mem_label = settings_label("Memory:", true);
            rx_text_view* mem_value = text_view_new("512 MB (from PMM)");
            if (mem_label) view_add_child(mem_row, (rx_view*)mem_label);
            if (mem_value) {
                mem_value->color = NX_COLOR_TEXT;
                view_add_child(mem_row, (rx_view*)mem_value);
            }
            view_add_child(hw_card, mem_row);
        }
        
        /* GPU */
        rx_view* gpu_row = hstack_new(8.0f);
        if (gpu_row) {
            rx_text_view* gpu_label = settings_label("GPU:", true);
            rx_text_view* gpu_value = text_view_new("NXGFX Compatible");
            if (gpu_label) view_add_child(gpu_row, (rx_view*)gpu_label);
            if (gpu_value) {
                gpu_value->color = NX_COLOR_TEXT;
                view_add_child(gpu_row, (rx_view*)gpu_value);
            }
            view_add_child(hw_card, gpu_row);
        }
        view_add_child(panel, hw_card);
    }
    
    /* Uptime Card */
    rx_view* uptime_card = settings_card("Uptime");
    if (uptime_card) {
        rx_text_view* uptime_value = text_view_new("0 days, 0 hours, 5 minutes");
        if (uptime_value) {
            uptime_value->color = NX_COLOR_TEXT;
            view_add_child(uptime_card, (rx_view*)uptime_value);
        }
        view_add_child(panel, uptime_card);
    }
    
    /* Updates Card */
    rx_view* update_card = settings_card("Software Updates");
    if (update_card) {
        rx_view* status_row = hstack_new(8.0f);
        if (status_row) {
            rx_text_view* status_icon = text_view_new("[check]");
            rx_text_view* status_text = text_view_new("System is up to date");
            if (status_icon) {
                status_icon->color = NX_COLOR_SUCCESS;
                view_add_child(status_row, (rx_view*)status_icon);
            }
            if (status_text) {
                status_text->color = NX_COLOR_TEXT;
                view_add_child(status_row, (rx_view*)status_text);
            }
            view_add_child(update_card, status_row);
        }
        
        rx_button_view* check_btn = button_view_new("Check for Updates");
        if (check_btn) view_add_child(update_card, (rx_view*)check_btn);
        
        view_add_child(panel, update_card);
    }
    
    return panel;
}
