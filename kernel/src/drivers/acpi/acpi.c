// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#include <kernel/init.h>
#include <drivers/acpi/acpi.h>
#include <stdint.h>
#include "io/io.h"
#include "utils/log.h"
#include <string.h>
#include "arch/paging.h"

// Global ACPI variables
static acpi_rsdp_t* rsdp = NULL;
static acpi_fadt_t* fadt = NULL;
static acpi_dsdt_t* dsdt = NULL;
static pm_registers_t pm_regs;
static uint8_t acpi_enabled = 0;

// Internal functions
static void* acpi_find_table(const char* signature);
static int acpi_checksum(void* table, uint32_t length);
static void acpi_parse_fadt();
static void acpi_setup_pm_registers();

// Initialize ACPI subsystem
int acpi_init(void) {
    // TODO: Initialize ACPI tables, power management, sleep states
    return 0;
}

core_initcall(acpi_init);

// Find ACPI table by signature
static void* acpi_find_table(const char* signature) {
    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        acpi_xsdt_t* xsdt = (acpi_xsdt_t*)(uintptr_t)rsdp->xsdt_address;
        uint32_t entries = (xsdt->header.length - sizeof(acpi_header_t)) / 8;
        
        for (uint32_t i = 0; i < entries; i++) {
            acpi_header_t* header = (acpi_header_t*)(uintptr_t)xsdt->entries[i];
            if (strncmp(header->signature, signature, 4) == 0 && 
                acpi_checksum(header, header->length) == 0) {
                return header;
            }
        }
    } else {
        acpi_rsdt_t* rsdt = (acpi_rsdt_t*)(uintptr_t)rsdp->rsdt_address;
        uint32_t entries = (rsdt->header.length - sizeof(acpi_header_t)) / 4;
        
        for (uint32_t i = 0; i < entries; i++) {
            acpi_header_t* header = (acpi_header_t*)(uintptr_t)rsdt->entries[i];
            if (strncmp(header->signature, signature, 4) == 0 && 
                acpi_checksum(header, header->length) == 0) {
                return header;
            }
        }
    }
    return NULL;
}

// Verify ACPI table checksum
static int acpi_checksum(void* table, uint32_t length) {
    uint8_t sum = 0;
    uint8_t* ptr = (uint8_t*)table;
    
    for (uint32_t i = 0; i < length; i++) {
        sum += ptr[i];
    }
    
    return sum == 0;
}

// Parse FADT and extract important information
static void acpi_parse_fadt() {
    // Use 64-bit addresses if available
    if (fadt->header.revision >= 2 && fadt->x_dsdt != 0) {
        dsdt = (acpi_dsdt_t*)(uintptr_t)fadt->x_dsdt;
    } else {
        dsdt = (acpi_dsdt_t*)(uintptr_t)fadt->dsdt;
    }

    log_info("DSDT at 0x%x", dsdt);
}

// Setup Power Management registers
static void acpi_setup_pm_registers() {
    // Use 64-bit addresses if available
    if (fadt->header.revision >= 2) {
        pm_regs.pm1a_cnt = (uint16_t)fadt->x_pm1a_cnt_blk;
        pm_regs.pm1b_cnt = (uint16_t)fadt->x_pm1b_cnt_blk;
    } else {
        pm_regs.pm1a_cnt = (uint16_t)fadt->pm1a_cnt_blk;
        pm_regs.pm1b_cnt = (uint16_t)fadt->pm1b_cnt_blk;
    }

    pm_regs.pm1_cnt_len = fadt->pm1_cnt_len;
    pm_regs.sleep_type_a = (fadt->pm1a_cnt_blk ? 0x2000 : 0);
    pm_regs.sleep_type_b = (fadt->pm1b_cnt_blk ? 0x2000 : 0);

    log_info("PM1a_CNT: 0x%x, PM1b_CNT: 0x%x", pm_regs.pm1a_cnt, pm_regs.pm1b_cnt);
}

// Enable ACPI
int acpi_enable() {
    if (!fadt || !fadt->smi_cmd || !fadt->acpi_enable) {
        return -1;
    }

    // Send ACPI enable command
    outb(fadt->smi_cmd, fadt->acpi_enable);

    // Wait for SCI_EN bit to be set in PM1_CNT
    uint16_t pm1_cnt = inw(pm_regs.pm1a_cnt);
    int timeout = 1000;
    
    while (!(pm1_cnt & 0x01) && timeout--) {
        io_wait();
        pm1_cnt = inw(pm_regs.pm1a_cnt);
    }

    if (timeout <= 0) {
        log_error("Timeout waiting for ACPI enable");
        return -1;
    }

    log_info("ACPI enabled");
    return 0;
}

// Disable ACPI
int acpi_disable() {
    if (!fadt || !fadt->smi_cmd || !fadt->acpi_disable) {
        return -1;
    }

    outb(fadt->smi_cmd, fadt->acpi_disable);
    acpi_enabled = 0;
    log_info("ACPI disabled");
    return 0;
}

// Power off the system
void acpi_poweroff() {
    if (!acpi_enabled) {
        log_error("ACPI not enabled, cannot power off");
        return;
    }

    // Check for S5 (soft power off) support
    if (!(fadt->flags & (1 << 13))) {
        log_error("S5 power state not supported");
        return;
    }

    // Enable power off
    outw(pm_regs.pm1a_cnt, (0x0 << 10) | (0x1 << 13) | (1 << 8));
    if (pm_regs.pm1b_cnt) {
        outw(pm_regs.pm1b_cnt, (0x0 << 10) | (0x1 << 13) | (1 << 8));
    }

    log_info("System powering off...");
    for(;;) asm("hlt");
}

// Reboot the system
void acpi_reboot() {
    // Try keyboard controller reset
    uint8_t temp;
    do {
        temp = inb(0x64);
        if (temp & 0x01) {
            inb(0x60);
        }
    } while (temp & 0x02);

    outb(0x64, 0xFE);
    log_info("System rebooting...");
    for(;;) asm("hlt");
}

// Enter sleep state
void acpi_sleep(sleep_state_t state) {
    if (!acpi_enabled || state < S1 || state > S5) {
        return;
    }

    log_info("Entering sleep state S%d", state);

    // Prepare for sleep
    uint16_t slp_typa = (state << 10) | (1 << 13);
    uint16_t slp_typb = (state << 10) | (1 << 13);

    // Write sleep type
    outw(pm_regs.pm1a_cnt, slp_typa);
    if (pm_regs.pm1b_cnt) {
        outw(pm_regs.pm1b_cnt, slp_typb);
    }

    // Write SLP_EN bit
    outw(pm_regs.pm1a_cnt, slp_typa | (1 << 8));
    if (pm_regs.pm1b_cnt) {
        outw(pm_regs.pm1b_cnt, slp_typb | (1 << 8));
    }

    // Wait for sleep
    for(;;) asm("hlt");
}

// Handle ACPI events
void acpi_handle_events() {
    // TODO: Implement GPE handling
}

// Thermal control
void acpi_thermal_control() {
    // TODO: Implement thermal zones monitoring
}

// Battery information
void acpi_battery_info() {
    // TODO: Implement battery status checking
}