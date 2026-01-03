#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

// ACPI Table Header
typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_header_t;

// Root System Description Pointer (RSDP)
typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

// System Description Table (RSDT/XSDT)
typedef struct {
    acpi_header_t header;
    uint32_t entries[];
} __attribute__((packed)) acpi_rsdt_t;

typedef struct {
    acpi_header_t header;
    uint64_t entries[];
} __attribute__((packed)) acpi_xsdt_t;

// Fixed ACPI Description Table (FADT)
typedef struct {
    acpi_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    
    uint8_t reserved;
    
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;
    
    // ACPI 2.0+ fields
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    
    uint64_t x_pm1a_evt_blk;
    uint64_t x_pm1b_evt_blk;
    uint64_t x_pm1a_cnt_blk;
    uint64_t x_pm1b_cnt_blk;
    uint64_t x_pm2_cnt_blk;
    uint64_t x_pm_tmr_blk;
    uint64_t x_gpe0_blk;
    uint64_t x_gpe1_blk;
} __attribute__((packed)) acpi_fadt_t;

// Differentiated System Description Table (DSDT)
typedef struct {
    acpi_header_t header;
    uint8_t definition_block[];
} __attribute__((packed)) acpi_dsdt_t;

// Power Management Control Registers
typedef struct {
    uint16_t pm1a_cnt;
    uint16_t pm1b_cnt;
    uint32_t pm1_cnt_len;
    uint32_t sleep_type_a;
    uint32_t sleep_type_b;
} pm_registers_t;

// Sleep States
typedef enum {
    S0 = 0,  // Working
    S1,      // Power on suspend
    S2,      // CPU powered off
    S3,      // Suspend to RAM
    S4,      // Hibernate
    S5       // Power off
} sleep_state_t;

// Function prototypes
void acpi_init();
void acpi_poweroff();
void acpi_reboot();
void acpi_sleep(sleep_state_t state);
int acpi_enable();
int acpi_disable();
void acpi_handle_events();
void acpi_thermal_control();
void acpi_battery_info();

#endif // ACPI_H