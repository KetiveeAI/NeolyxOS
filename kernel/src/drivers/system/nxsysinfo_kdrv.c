/*
 * NXSysInfo Kernel Driver Implementation
 * 
 * System Information & Sensor Detection
 * 
 * Internal structure:
 *   - cpu.c logic: CPUID parsing
 *   - mem.c logic: Memory detection
 *   - board.c logic: SMBIOS/DMI parsing
 *   - thermal.c logic: ACPI thermal zones
 *   - input_state.c logic: EC/ACPI lid state
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxsysinfo_kdrv.h"
#include <stdint.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Helpers ============ */

static inline void nx_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static inline void nx_memset(void *p, int c, size_t n) {
    uint8_t *ptr = (uint8_t*)p;
    while (n--) *ptr++ = (uint8_t)c;
}

static void serial_dec(int val) {
    char buf[12];
    int i = 0;
    if (val < 0) { serial_putc('-'); val = -val; }
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

/* ============ CPUID Access ============ */

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, 
                         uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

static inline void cpuid_ex(uint32_t leaf, uint32_t subleaf,
                            uint32_t *eax, uint32_t *ebx, 
                            uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}

/* ============ Cached State ============ */

#define MAX_THERMAL_SENSORS    8

static struct {
    int                     initialized;
    
    /* Cached at init */
    nxsysinfo_cpu_t         cpu;
    nxsysinfo_board_t       board;
    nxsysinfo_mem_t         mem;
    
    /* Dynamic */
    int                     thermal_count;
    nxsysinfo_thermal_t     thermals[MAX_THERMAL_SENSORS];
    nxsysinfo_input_state_t input_state;
    nxsysinfo_battery_t     battery;
} g_sysinfo = {0};

/* ============ CPU Detection (cached) ============ */

static void detect_cpu(void) {
    uint32_t eax, ebx, ecx, edx;
    
    /* Vendor string */
    cpuid(0, &eax, &ebx, &ecx, &edx);
    *((uint32_t*)&g_sysinfo.cpu.vendor_str[0]) = ebx;
    *((uint32_t*)&g_sysinfo.cpu.vendor_str[4]) = edx;
    *((uint32_t*)&g_sysinfo.cpu.vendor_str[8]) = ecx;
    g_sysinfo.cpu.vendor_str[12] = '\0';
    
    /* Determine vendor */
    if (g_sysinfo.cpu.vendor_str[0] == 'G') {
        g_sysinfo.cpu.vendor = NXCPU_VENDOR_INTEL;
    } else if (g_sysinfo.cpu.vendor_str[0] == 'A') {
        g_sysinfo.cpu.vendor = NXCPU_VENDOR_AMD;
    } else {
        g_sysinfo.cpu.vendor = NXCPU_VENDOR_UNKNOWN;
    }
    
    /* Family/Model/Stepping */
    cpuid(1, &eax, &ebx, &ecx, &edx);
    g_sysinfo.cpu.stepping = eax & 0xF;
    g_sysinfo.cpu.model = ((eax >> 4) & 0xF) | (((eax >> 16) & 0xF) << 4);
    g_sysinfo.cpu.family = ((eax >> 8) & 0xF) + ((eax >> 20) & 0xFF);
    
    /* Features */
    g_sysinfo.cpu.has_sse = (edx & (1 << 25)) ? 1 : 0;
    g_sysinfo.cpu.has_aes = (ecx & (1 << 25)) ? 1 : 0;
    g_sysinfo.cpu.has_avx = (ecx & (1 << 28)) ? 1 : 0;
    g_sysinfo.cpu.has_hyperthreading = (edx & (1 << 28)) ? 1 : 0;
    
    /* AVX2 check */
    cpuid_ex(7, 0, &eax, &ebx, &ecx, &edx);
    g_sysinfo.cpu.has_avx2 = (ebx & (1 << 5)) ? 1 : 0;
    g_sysinfo.cpu.has_avx512 = (ebx & (1 << 16)) ? 1 : 0;
    
    /* Brand string (leaf 0x80000002-0x80000004) */
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004) {
        uint32_t *brand = (uint32_t*)g_sysinfo.cpu.brand;
        for (int i = 0; i < 3; i++) {
            cpuid(0x80000002 + i, &brand[i*4], &brand[i*4+1], 
                  &brand[i*4+2], &brand[i*4+3]);
        }
        g_sysinfo.cpu.brand[48] = '\0';
    }
    
    /* Topology (simplified - use CPUID leaf 0xB for full) */
    cpuid(1, &eax, &ebx, &ecx, &edx);
    g_sysinfo.cpu.threads = (ebx >> 16) & 0xFF;
    if (g_sysinfo.cpu.threads == 0) g_sysinfo.cpu.threads = 1;
    
    g_sysinfo.cpu.cores = g_sysinfo.cpu.threads;
    if (g_sysinfo.cpu.has_hyperthreading) {
        g_sysinfo.cpu.cores = g_sysinfo.cpu.threads / 2;
        if (g_sysinfo.cpu.cores == 0) g_sysinfo.cpu.cores = 1;
    }
    g_sysinfo.cpu.sockets = 1;
    
    /* Frequency (from CPUID 0x16 if available) */
    cpuid(0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x16) {
        cpuid(0x16, &eax, &ebx, &ecx, &edx);
        g_sysinfo.cpu.base_freq_mhz = eax & 0xFFFF;
        g_sysinfo.cpu.max_freq_mhz = ebx & 0xFFFF;
        g_sysinfo.cpu.current_freq_mhz = g_sysinfo.cpu.base_freq_mhz;
    } else {
        /* Estimate from brand string or default */
        g_sysinfo.cpu.base_freq_mhz = 2000;
        g_sysinfo.cpu.max_freq_mhz = 4000;
        g_sysinfo.cpu.current_freq_mhz = 2000;
    }
    
    /* Cache sizes (simplified) */
    g_sysinfo.cpu.l1_cache_kb = 64;
    g_sysinfo.cpu.l2_cache_kb = 512;
    g_sysinfo.cpu.l3_cache_kb = 8192;
}

/* ============ Memory Detection ============ */

static void detect_memory(void) {
    /* Get from boot info or estimate */
    /* For now, use defaults that would be set by bootloader */
    extern uint64_t boot_memory_size;  /* Set by bootloader */
    
    g_sysinfo.mem.total_bytes = 4ULL * 1024 * 1024 * 1024;  /* Default 4GB */
    g_sysinfo.mem.available_bytes = 3ULL * 1024 * 1024 * 1024;
    g_sysinfo.mem.used_bytes = 1ULL * 1024 * 1024 * 1024;
    g_sysinfo.mem.speed_mhz = 3200;
    g_sysinfo.mem.channels = 2;
    nx_strcpy(g_sysinfo.mem.type, "DDR4", 16);
}

/* ============ Board Detection ============ */

static void detect_board(void) {
    /* Would parse SMBIOS/DMI tables */
    /* For now, use QEMU defaults */
    nx_strcpy(g_sysinfo.board.manufacturer, "NeolyxOS", 64);
    nx_strcpy(g_sysinfo.board.product, "Virtual Machine", 64);
    nx_strcpy(g_sysinfo.board.version, "1.0", 32);
    nx_strcpy(g_sysinfo.board.serial, "NEOLYX-001", 32);
    nx_strcpy(g_sysinfo.board.bios_vendor, "NeolyxOS UEFI", 64);
    nx_strcpy(g_sysinfo.board.bios_version, "1.0.0", 32);
    nx_strcpy(g_sysinfo.board.bios_date, "2025-01-01", 16);
}

/* ============ Thermal Sensor Detection ============ */

static void detect_thermals(void) {
    g_sysinfo.thermal_count = 0;
    
    /* CPU thermal */
    nxsysinfo_thermal_t *cpu_thermal = &g_sysinfo.thermals[g_sysinfo.thermal_count++];
    cpu_thermal->id = 0;
    cpu_thermal->type = NXTHERMAL_CPU;
    nx_strcpy(cpu_thermal->name, "CPU Package", 32);
    cpu_thermal->temp_celsius = 450;  /* 45.0°C */
    cpu_thermal->temp_max = 1000;     /* 100°C */
    cpu_thermal->temp_critical = 1050;
    cpu_thermal->temp_target = 800;
    cpu_thermal->valid = 1;
    
    /* GPU thermal (if present) */
    nxsysinfo_thermal_t *gpu_thermal = &g_sysinfo.thermals[g_sysinfo.thermal_count++];
    gpu_thermal->id = 1;
    gpu_thermal->type = NXTHERMAL_GPU;
    nx_strcpy(gpu_thermal->name, "GPU", 32);
    gpu_thermal->temp_celsius = 400;
    gpu_thermal->temp_max = 950;
    gpu_thermal->temp_critical = 1000;
    gpu_thermal->temp_target = 750;
    gpu_thermal->valid = 1;
    
    /* SSD thermal */
    nxsysinfo_thermal_t *ssd_thermal = &g_sysinfo.thermals[g_sysinfo.thermal_count++];
    ssd_thermal->id = 2;
    ssd_thermal->type = NXTHERMAL_SSD;
    nx_strcpy(ssd_thermal->name, "NVMe SSD", 32);
    ssd_thermal->temp_celsius = 350;
    ssd_thermal->temp_max = 700;
    ssd_thermal->temp_critical = 750;
    ssd_thermal->temp_target = 500;
    ssd_thermal->valid = 1;
}

/* ============ Input State Detection ============ */

static void detect_input_state(void) {
    /* Would query EC/ACPI */
    g_sysinfo.input_state.lid_state = NXLID_OPEN;
    g_sysinfo.input_state.touchpad_locked = 0;
    g_sysinfo.input_state.keyboard_locked = 0;
    g_sysinfo.input_state.fn_lock = 0;
}

/* ============ Battery Detection ============ */

static void detect_battery(void) {
    /* Would query ACPI battery */
    g_sysinfo.battery.present = 1;
    g_sysinfo.battery.charging = 0;
    g_sysinfo.battery.percent = 85;
    g_sysinfo.battery.current_mw = -12000;  /* Discharging */
    g_sysinfo.battery.voltage_mv = 11400;
    g_sysinfo.battery.capacity_mwh = 45000;
    g_sysinfo.battery.design_capacity_mwh = 52000;
    g_sysinfo.battery.time_to_empty_min = 180;
    g_sysinfo.battery.time_to_full_min = -1;
    g_sysinfo.battery.cycle_count = 125;
    g_sysinfo.battery.temp_celsius = 280;  /* 28.0°C */
    nx_strcpy(g_sysinfo.battery.manufacturer, "Generic", 32);
    nx_strcpy(g_sysinfo.battery.model, "Li-Ion 45Wh", 32);
    nx_strcpy(g_sysinfo.battery.chemistry, "Li-Ion", 8);
}

/* ============ Public API ============ */

int nxsysinfo_kdrv_init(void) {
    if (g_sysinfo.initialized) {
        return 0;
    }
    
    serial_puts("[NXSysInfo] Initializing v" NXSYSINFO_KDRV_VERSION "\n");
    
    nx_memset(&g_sysinfo, 0, sizeof(g_sysinfo));
    
    /* Cache static info */
    detect_cpu();
    detect_board();
    detect_memory();
    
    /* Detect sensors */
    detect_thermals();
    detect_input_state();
    detect_battery();
    
    g_sysinfo.initialized = 1;
    
    serial_puts("[NXSysInfo] CPU: ");
    serial_puts(g_sysinfo.cpu.brand);
    serial_puts("\n");
    serial_puts("[NXSysInfo] ");
    serial_dec(g_sysinfo.cpu.cores);
    serial_puts(" cores, ");
    serial_dec(g_sysinfo.cpu.threads);
    serial_puts(" threads\n");
    serial_puts("[NXSysInfo] ");
    serial_dec(g_sysinfo.thermal_count);
    serial_puts(" thermal sensors\n");
    
    return 0;
}

void nxsysinfo_kdrv_shutdown(void) {
    if (!g_sysinfo.initialized) return;
    serial_puts("[NXSysInfo] Shutting down\n");
    g_sysinfo.initialized = 0;
}

int nxsysinfo_kdrv_cpu_info(nxsysinfo_cpu_t *info) {
    if (!info) return -1;
    *info = g_sysinfo.cpu;
    return 0;
}

uint32_t nxsysinfo_kdrv_cpu_freq_mhz(void) {
    /* Would read MSR for actual frequency */
    return g_sysinfo.cpu.current_freq_mhz;
}

int nxsysinfo_kdrv_mem_info(nxsysinfo_mem_t *info) {
    if (!info) return -1;
    *info = g_sysinfo.mem;
    return 0;
}

int nxsysinfo_kdrv_board_info(nxsysinfo_board_t *info) {
    if (!info) return -1;
    *info = g_sysinfo.board;
    return 0;
}

int nxsysinfo_kdrv_thermal_count(void) {
    return g_sysinfo.thermal_count;
}

int nxsysinfo_kdrv_thermal_info(int index, nxsysinfo_thermal_t *info) {
    if (index < 0 || index >= g_sysinfo.thermal_count) return -1;
    if (!info) return -2;
    *info = g_sysinfo.thermals[index];
    return 0;
}

int nxsysinfo_kdrv_input_state(nxsysinfo_input_state_t *state) {
    if (!state) return -1;
    detect_input_state();  /* Refresh */
    *state = g_sysinfo.input_state;
    return 0;
}

int nxsysinfo_kdrv_battery_info(nxsysinfo_battery_t *info) {
    if (!info) return -1;
    detect_battery();  /* Refresh */
    *info = g_sysinfo.battery;
    return 0;
}

void nxsysinfo_kdrv_debug(void) {
    serial_puts("\n=== NXSysInfo Debug ===\n");
    serial_puts("CPU: ");
    serial_puts(g_sysinfo.cpu.brand);
    serial_puts("\n  Vendor: ");
    serial_puts(g_sysinfo.cpu.vendor_str);
    serial_puts("\n  Cores: ");
    serial_dec(g_sysinfo.cpu.cores);
    serial_puts("  Threads: ");
    serial_dec(g_sysinfo.cpu.threads);
    serial_puts("\n  Base Freq: ");
    serial_dec(g_sysinfo.cpu.base_freq_mhz);
    serial_puts(" MHz\n");
    serial_puts("  Features: ");
    if (g_sysinfo.cpu.has_sse) serial_puts("SSE ");
    if (g_sysinfo.cpu.has_avx) serial_puts("AVX ");
    if (g_sysinfo.cpu.has_avx2) serial_puts("AVX2 ");
    if (g_sysinfo.cpu.has_aes) serial_puts("AES ");
    serial_puts("\n\n");
    
    serial_puts("Thermals:\n");
    for (int i = 0; i < g_sysinfo.thermal_count; i++) {
        nxsysinfo_thermal_t *t = &g_sysinfo.thermals[i];
        serial_puts("  ");
        serial_puts(t->name);
        serial_puts(": ");
        serial_dec(t->temp_celsius / 10);
        serial_puts(".");
        serial_dec(t->temp_celsius % 10);
        serial_puts("°C\n");
    }
    
    serial_puts("\nBattery: ");
    serial_dec(g_sysinfo.battery.percent);
    serial_puts("% ");
    serial_puts(g_sysinfo.battery.charging ? "(charging)" : "(discharging)");
    serial_puts("\n");
    
    serial_puts("========================\n\n");
}
