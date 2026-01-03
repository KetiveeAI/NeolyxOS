/*
 * NXSysInfo Kernel Driver (nxsysinfo.kdrv)
 * 
 * NeolyxOS System Information & Sensor Driver
 * 
 * Architecture:
 *   [ NXSysInfo Kernel Driver ]
 *        ↕ Hardware Detection
 *   [ CPUID | ACPI | SMBus | EC ]
 *        ↕ Kernel API
 *   [ System Monitor | Settings ]
 * 
 * Features:
 *   - CPU info (model, cores, frequency)
 *   - Memory info (total, speed)
 *   - Motherboard/BIOS info
 *   - Thermal sensors (enumeration-based)
 *   - Lid/lock state
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXSYSINFO_KDRV_H
#define NXSYSINFO_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXSYSINFO_KDRV_VERSION    "1.0.0"
#define NXSYSINFO_KDRV_ABI        1

/* ============ CPU Info ============ */

typedef enum {
    NXCPU_VENDOR_UNKNOWN = 0,
    NXCPU_VENDOR_INTEL,
    NXCPU_VENDOR_AMD,
    NXCPU_VENDOR_ARM,
    NXCPU_VENDOR_APPLE,
} nxcpu_vendor_t;

typedef struct {
    nxcpu_vendor_t  vendor;
    char            vendor_str[16];
    char            brand[64];
    uint32_t        family;
    uint32_t        model;
    uint32_t        stepping;
    
    /* Topology */
    uint32_t        cores;
    uint32_t        threads;
    uint32_t        sockets;
    
    /* Frequency (cached at init, updated on demand) */
    uint32_t        base_freq_mhz;
    uint32_t        max_freq_mhz;
    uint32_t        current_freq_mhz;
    
    /* Cache */
    uint32_t        l1_cache_kb;
    uint32_t        l2_cache_kb;
    uint32_t        l3_cache_kb;
    
    /* Features */
    uint8_t         has_sse;
    uint8_t         has_avx;
    uint8_t         has_avx2;
    uint8_t         has_avx512;
    uint8_t         has_aes;
    uint8_t         has_hyperthreading;
} nxsysinfo_cpu_t;

/* ============ Memory Info ============ */

typedef struct {
    uint64_t        total_bytes;
    uint64_t        available_bytes;
    uint64_t        used_bytes;
    uint32_t        speed_mhz;
    uint8_t         channels;
    char            type[16];           /* DDR4, DDR5, etc. */
} nxsysinfo_mem_t;

/* ============ Board Info ============ */

typedef struct {
    char            manufacturer[64];
    char            product[64];
    char            version[32];
    char            serial[32];
    char            bios_vendor[64];
    char            bios_version[32];
    char            bios_date[16];
} nxsysinfo_board_t;

/* ============ Thermal Sensors (Enumeration-based) ============ */

typedef enum {
    NXTHERMAL_CPU = 0,
    NXTHERMAL_GPU,
    NXTHERMAL_CHIPSET,
    NXTHERMAL_AMBIENT,
    NXTHERMAL_SSD,
    NXTHERMAL_BATTERY,
    NXTHERMAL_OTHER,
} nxthermal_type_t;

typedef struct {
    uint32_t        id;
    nxthermal_type_t type;
    char            name[32];
    int32_t         temp_celsius;       /* Current temp (x10 for 0.1 precision) */
    int32_t         temp_max;           /* Max safe temp */
    int32_t         temp_critical;      /* Critical shutdown temp */
    int32_t         temp_target;        /* Target/throttle temp */
    uint8_t         valid;
} nxsysinfo_thermal_t;

/* ============ Input/Lid State ============ */

typedef enum {
    NXLID_UNKNOWN = 0,
    NXLID_OPEN,
    NXLID_CLOSED,
} nxlid_state_t;

typedef struct {
    nxlid_state_t   lid_state;
    uint8_t         touchpad_locked;
    uint8_t         keyboard_locked;
    uint8_t         fn_lock;
} nxsysinfo_input_state_t;

/* ============ Battery Info ============ */

typedef struct {
    uint8_t         present;
    uint8_t         charging;
    uint8_t         percent;
    int32_t         current_mw;         /* Positive = charging */
    int32_t         voltage_mv;
    int32_t         capacity_mwh;
    int32_t         design_capacity_mwh;
    int32_t         time_to_empty_min;
    int32_t         time_to_full_min;
    int32_t         cycle_count;
    int32_t         temp_celsius;
    char            manufacturer[32];
    char            model[32];
    char            chemistry[8];       /* Li-Ion, etc. */
} nxsysinfo_battery_t;

/* ============ Kernel Driver API ============ */

/**
 * Initialize system info subsystem
 * Caches static info (CPU model, board, etc.)
 */
int nxsysinfo_kdrv_init(void);

/**
 * Shutdown
 */
void nxsysinfo_kdrv_shutdown(void);

/**
 * Get CPU info (cached)
 */
int nxsysinfo_kdrv_cpu_info(nxsysinfo_cpu_t *info);

/**
 * Get current CPU frequency (live poll)
 */
uint32_t nxsysinfo_kdrv_cpu_freq_mhz(void);

/**
 * Get memory info
 */
int nxsysinfo_kdrv_mem_info(nxsysinfo_mem_t *info);

/**
 * Get board/BIOS info (cached)
 */
int nxsysinfo_kdrv_board_info(nxsysinfo_board_t *info);

/**
 * Get thermal sensor count
 */
int nxsysinfo_kdrv_thermal_count(void);

/**
 * Get thermal sensor info by index
 */
int nxsysinfo_kdrv_thermal_info(int index, nxsysinfo_thermal_t *info);

/**
 * Get input state (lid, locks)
 */
int nxsysinfo_kdrv_input_state(nxsysinfo_input_state_t *state);

/**
 * Get battery info
 */
int nxsysinfo_kdrv_battery_info(nxsysinfo_battery_t *info);

/**
 * Debug output
 */
void nxsysinfo_kdrv_debug(void);

#endif /* NXSYSINFO_KDRV_H */
