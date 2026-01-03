/*
 * NeolyxOS Settings - About Device Panel
 * 
 * Device information, PC rename, and system controls:
 * - Device/Computer name
 * - Serial number and model
 * - System version and build
 * - Hardware info (CPU, RAM, GPU, Storage)
 * - Rename device functionality
 * - System controls (restart, shutdown, recovery)
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Device Settings State
 * ============================================================================ */

static struct {
    char device_name[64];
    char model_name[32];
    char model_number[32];
    char serial_number[24];
    
    /* System */
    char os_version[32];
    char build_number[32];
    char kernel_version[32];
    
    /* Hardware */
    char cpu_name[64];
    uint32_t cpu_cores;
    uint64_t ram_total_mb;
    uint64_t ram_used_mb;
    char gpu_name[64];
    uint64_t storage_total_gb;
    uint64_t storage_used_gb;
    
    /* Network */
    char mac_address[24];
    char ip_address[24];
    
    /* Date */
    char install_date[32];
    uint64_t uptime_seconds;
    
    /* Rename state */
    bool is_editing_name;
    char edit_buffer[64];
} g_device_info = {
    .device_name = "swana's NeolyxOS",
    .model_name = "Custom PC",
    .model_number = "NEO-2025",
    .serial_number = "NXOS-XXXX-XXXX-XXXX",
    
    .os_version = "1.0.0",
    .build_number = "20251227",
    .kernel_version = "NeolyxOS Kernel 1.0",
    
    .cpu_name = "CPU via CPUID",
    .cpu_cores = 4,
    .ram_total_mb = 16384,
    .ram_used_mb = 4096,
    .gpu_name = "NXGFX Compatible",
    .storage_total_gb = 500,
    .storage_used_gb = 120,
    
    .mac_address = "00:00:00:00:00:00",
    .ip_address = "0.0.0.0",
    
    .install_date = "December 2025",
    .uptime_seconds = 0,
    
    .is_editing_name = false,
    .edit_buffer = ""
};

/* Forward declarations for system functions */
extern void system_reboot(void);
extern void system_shutdown(void);
extern void system_enter_recovery(void);
extern int hostname_set(const char *name);
extern int hostname_get(char *buf, int len);

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void format_uptime(uint64_t seconds, char *buf, int len) {
    uint64_t days = seconds / 86400;
    uint64_t hours = (seconds % 86400) / 3600;
    uint64_t mins = (seconds % 3600) / 60;
    
    if (days > 0) {
        snprintf(buf, len, "%llu days, %llu hours", 
                 (unsigned long long)days, (unsigned long long)hours);
    } else if (hours > 0) {
        snprintf(buf, len, "%llu hours, %llu minutes", 
                 (unsigned long long)hours, (unsigned long long)mins);
    } else {
        snprintf(buf, len, "%llu minutes", (unsigned long long)mins);
    }
}

static void format_storage(uint64_t used, uint64_t total, char *buf, int len) {
    snprintf(buf, len, "%llu GB used of %llu GB", 
             (unsigned long long)used, (unsigned long long)total);
}

static void format_memory(uint64_t used, uint64_t total, char *buf, int len) {
    snprintf(buf, len, "%.1f GB used of %.1f GB", 
             used / 1024.0, total / 1024.0);
}

/* ============================================================================
 * Device Identity Card
 * ============================================================================ */

static rx_view* create_device_identity_card(void) {
    rx_view* card = settings_card("About This Device");
    if (!card) return NULL;
    
    /* NeolyxOS Logo / Icon area */
    rx_view* logo_row = hstack_new(16.0f);
    if (logo_row) {
        /* Large NeolyxOS text as logo */
        rx_text_view* logo = text_view_new("NeolyxOS");
        if (logo) {
            text_view_set_font_size(logo, 32.0f);
            logo->font_weight = 800;
            logo->color = NX_COLOR_PRIMARY;
            view_add_child(logo_row, (rx_view*)logo);
        }
        view_add_child(card, logo_row);
    }
    
    /* Device name with edit button */
    rx_view* name_section = settings_section("DEVICE NAME");
    if (name_section) view_add_child(card, name_section);
    
    rx_view* name_row = hstack_new(8.0f);
    if (name_row) {
        rx_text_view* name_label = text_view_new(g_device_info.device_name);
        if (name_label) {
            text_view_set_font_size(name_label, 18.0f);
            name_label->font_weight = 600;
            name_label->color = NX_COLOR_TEXT;
            view_add_child(name_row, (rx_view*)name_label);
        }
        
        /* Edit/Rename button */
        rx_button_view* edit_btn = button_view_new("Rename...");
        if (edit_btn) {
            edit_btn->normal_color = NX_COLOR_PRIMARY;
            view_add_child(name_row, (rx_view*)edit_btn);
        }
        view_add_child(card, name_row);
    }
    
    /* Model and serial info */
    rx_view* model_row = settings_row("Model", g_device_info.model_name);
    if (model_row) view_add_child(card, model_row);
    
    rx_view* model_num = settings_row("Model Number", g_device_info.model_number);
    if (model_num) view_add_child(card, model_num);
    
    rx_view* serial_row = settings_row("Serial Number", g_device_info.serial_number);
    if (serial_row) view_add_child(card, serial_row);
    
    return card;
}

/* ============================================================================
 * System Version Card
 * ============================================================================ */

static rx_view* create_system_version_card(void) {
    rx_view* card = settings_card("System");
    if (!card) return NULL;
    
    /* OS Version */
    char version_str[64];
    snprintf(version_str, sizeof(version_str), "%s (Build %s)", 
             g_device_info.os_version, g_device_info.build_number);
    rx_view* version_row = settings_row("NeolyxOS Version", version_str);
    if (version_row) view_add_child(card, version_row);
    
    /* Kernel */
    rx_view* kernel_row = settings_row("Kernel", g_device_info.kernel_version);
    if (kernel_row) view_add_child(card, kernel_row);
    
    /* Architecture */
    rx_view* arch_row = settings_row("Architecture", "x86_64");
    if (arch_row) view_add_child(card, arch_row);
    
    /* UI Framework */
    rx_view* ui_row = settings_row("UI Framework", "ReoxUI 1.0");
    if (ui_row) view_add_child(card, ui_row);
    
    /* Install date */
    rx_view* install_row = settings_row("Installed", g_device_info.install_date);
    if (install_row) view_add_child(card, install_row);
    
    /* Uptime */
    char uptime_str[64];
    format_uptime(g_device_info.uptime_seconds, uptime_str, sizeof(uptime_str));
    rx_view* uptime_row = settings_row("System Uptime", uptime_str);
    if (uptime_row) view_add_child(card, uptime_row);
    
    return card;
}

/* ============================================================================
 * Hardware Info Card
 * ============================================================================ */

static rx_view* create_hardware_info_card(void) {
    rx_view* card = settings_card("Hardware");
    if (!card) return NULL;
    
    /* CPU */
    char cpu_str[96];
    snprintf(cpu_str, sizeof(cpu_str), "%s (%u cores)", 
             g_device_info.cpu_name, g_device_info.cpu_cores);
    rx_view* cpu_row = settings_row("Processor", cpu_str);
    if (cpu_row) view_add_child(card, cpu_row);
    
    /* Memory */
    char mem_str[64];
    format_memory(g_device_info.ram_used_mb, g_device_info.ram_total_mb, 
                  mem_str, sizeof(mem_str));
    rx_view* mem_row = settings_row("Memory", mem_str);
    if (mem_row) view_add_child(card, mem_row);
    
    /* GPU */
    rx_view* gpu_row = settings_row("Graphics", g_device_info.gpu_name);
    if (gpu_row) view_add_child(card, gpu_row);
    
    /* Storage */
    char storage_str[64];
    format_storage(g_device_info.storage_used_gb, g_device_info.storage_total_gb,
                   storage_str, sizeof(storage_str));
    rx_view* storage_row = settings_row("Storage", storage_str);
    if (storage_row) view_add_child(card, storage_row);
    
    /* Storage progress bar visualization */
    rx_view* storage_bar = hstack_new(4.0f);
    if (storage_bar) {
        float usage_percent = (float)g_device_info.storage_used_gb / 
                              (float)g_device_info.storage_total_gb * 100.0f;
        char percent_str[16];
        snprintf(percent_str, sizeof(percent_str), "%.0f%% used", usage_percent);
        rx_text_view* percent_label = settings_label(percent_str, true);
        if (percent_label) {
            percent_label->color = (usage_percent > 90) ? 
                (rx_color){255, 59, 48, 255} : NX_COLOR_TEXT_DIM;
            view_add_child(storage_bar, (rx_view*)percent_label);
        }
        view_add_child(card, storage_bar);
    }
    
    /* System information button */
    rx_button_view* info_btn = button_view_new("More Hardware Details...");
    if (info_btn) {
        info_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)info_btn);
    }
    
    return card;
}

/* ============================================================================
 * Network Info Card
 * ============================================================================ */

static rx_view* create_network_info_card(void) {
    rx_view* card = settings_card("Network");
    if (!card) return NULL;
    
    /* MAC Address */
    rx_view* mac_row = settings_row("MAC Address", g_device_info.mac_address);
    if (mac_row) view_add_child(card, mac_row);
    
    /* IP Address */
    rx_view* ip_row = settings_row("IP Address", g_device_info.ip_address);
    if (ip_row) view_add_child(card, ip_row);
    
    return card;
}

/* ============================================================================
 * System Controls Card
 * ============================================================================ */

static rx_view* create_system_controls_card(void) {
    rx_view* card = settings_card("System Controls");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new("Control system power and recovery options.");
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Restart button */
    rx_button_view* restart_btn = button_view_new("Restart...");
    if (restart_btn) {
        restart_btn->normal_color = (rx_color){ 10, 132, 255, 255 };
        view_add_child(card, (rx_view*)restart_btn);
    }
    
    /* Shutdown button */
    rx_button_view* shutdown_btn = button_view_new("Shut Down...");
    if (shutdown_btn) {
        shutdown_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)shutdown_btn);
    }
    
    /* Recovery mode */
    rx_view* recovery_section = settings_section("ADVANCED");
    if (recovery_section) view_add_child(card, recovery_section);
    
    rx_button_view* recovery_btn = button_view_new("Restart in Recovery Mode...");
    if (recovery_btn) {
        recovery_btn->normal_color = (rx_color){ 255, 149, 0, 255 };
        view_add_child(card, (rx_view*)recovery_btn);
    }
    
    /* Reset options */
    rx_button_view* reset_btn = button_view_new("Erase All Content and Settings...");
    if (reset_btn) {
        reset_btn->normal_color = (rx_color){ 255, 59, 48, 255 };
        view_add_child(card, (rx_view*)reset_btn);
    }
    
    return card;
}

/* ============================================================================
 * Legal & Support Card
 * ============================================================================ */

static rx_view* create_legal_card(void) {
    rx_view* card = settings_card("Legal & Regulatory");
    if (!card) return NULL;
    
    /* Copyright */
    rx_text_view* copyright = settings_label(
        "Copyright (c) 2025 KetiveeAI. All rights reserved.", true);
    if (copyright) view_add_child(card, (rx_view*)copyright);
    
    /* License links */
    rx_button_view* license_btn = button_view_new("Software License Agreement");
    if (license_btn) {
        license_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)license_btn);
    }
    
    rx_button_view* privacy_btn = button_view_new("Privacy Policy");
    if (privacy_btn) {
        privacy_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)privacy_btn);
    }
    
    rx_button_view* oss_btn = button_view_new("Open Source Licenses");
    if (oss_btn) {
        oss_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)oss_btn);
    }
    
    rx_button_view* regulatory_btn = button_view_new("Regulatory Compliance");
    if (regulatory_btn) {
        regulatory_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)regulatory_btn);
    }
    
    return card;
}

/* ============================================================================
 * Support Card
 * ============================================================================ */

static rx_view* create_support_card(void) {
    rx_view* card = settings_card("Support & Feedback");
    if (!card) return NULL;
    
    rx_button_view* help_btn = button_view_new("NeolyxOS Help & Documentation");
    if (help_btn) {
        help_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)help_btn);
    }
    
    rx_button_view* diag_btn = button_view_new("Run System Diagnostics");
    if (diag_btn) {
        diag_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)diag_btn);
    }
    
    rx_button_view* report_btn = button_view_new("Send Feedback to KetiveeAI");
    if (report_btn) {
        report_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)report_btn);
    }
    
    rx_button_view* logs_btn = button_view_new("Export System Logs");
    if (logs_btn) {
        logs_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)logs_btn);
    }
    
    return card;
}

/* ============================================================================
 * Main Panel
 * ============================================================================ */

rx_view* about_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("About");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Device identity card */
    rx_view* identity_card = create_device_identity_card();
    if (identity_card) view_add_child(panel, identity_card);
    
    /* System version card */
    rx_view* system_card = create_system_version_card();
    if (system_card) view_add_child(panel, system_card);
    
    /* Hardware info card */
    rx_view* hw_card = create_hardware_info_card();
    if (hw_card) view_add_child(panel, hw_card);
    
    /* Network info card */
    rx_view* net_card = create_network_info_card();
    if (net_card) view_add_child(panel, net_card);
    
    /* System controls card */
    rx_view* controls_card = create_system_controls_card();
    if (controls_card) view_add_child(panel, controls_card);
    
    /* Legal card */
    rx_view* legal_card = create_legal_card();
    if (legal_card) view_add_child(panel, legal_card);
    
    /* Support card */
    rx_view* support_card = create_support_card();
    if (support_card) view_add_child(panel, support_card);
    
    println("[About] Device info panel created");
    return panel;
}
