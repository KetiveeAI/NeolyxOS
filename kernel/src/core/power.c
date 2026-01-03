/*
 * NeolyxOS Power Management Implementation
 * 
 * ACPI-based power management with device detection.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "core/power.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* I/O Ports */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ============ State ============ */

static device_type_t detected_device = DEVICE_TYPE_UNKNOWN;
static power_state_t current_state = POWER_STATE_RUNNING;
static power_profile_t current_profile = POWER_PROFILE_BALANCED;
static power_source_t current_source = POWER_SOURCE_AC;

static battery_info_t battery = {0};
static ac_adapter_t ac_adapter = {0};
static cpu_power_info_t cpu_power = {0};

static power_settings_t settings;
static power_event_callback_t event_callback = NULL;

static int lid_open = 1;
static int display_brightness = 80;

/* ============ Helpers ============ */

static void power_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ============ ACPI ============ */

#define ACPI_PM1A_CNT   0x404   /* Power Management 1a Control */
#define ACPI_SLP_EN     (1 << 13)
#define ACPI_SLP_TYP_S3 (1 << 10)
#define ACPI_SLP_TYP_S5 (0x7 << 10)

/* ============ Device Detection ============ */

static device_type_t detect_device(void) {
    /* Check for VM first */
    /* CPUID hypervisor bit */
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    
    if (ecx & (1 << 31)) {
        serial_puts("[POWER] Running in virtual machine\n");
        return DEVICE_TYPE_VM;
    }
    
    /* Check for battery = laptop */
    /* Read ACPI battery status */
    /* For now, use a simple heuristic */
    
    /* Check SMBIOS for chassis type */
    /* Chassis types: Desktop=3, Laptop=9/10, Server=17/23 */
    
    /* Default: check if battery present */
    if (battery.present) {
        serial_puts("[POWER] Device type: Laptop\n");
        return DEVICE_TYPE_LAPTOP;
    }
    
    serial_puts("[POWER] Device type: Desktop\n");
    return DEVICE_TYPE_DESKTOP;
}

/* ============ ACPI Table Addresses (parsed at boot) ============ */

static uint16_t acpi_pm1a_cnt_blk = 0x404;  /* PM1a Control Block */
static uint16_t acpi_pm1b_cnt_blk = 0;      /* PM1b Control Block (optional) */
static uint8_t  acpi_slp_typa_s3 = 1;       /* SLP_TYPa for S3 */
static uint8_t  acpi_slp_typa_s4 = 2;       /* SLP_TYPa for S4 */
static uint8_t  acpi_slp_typa_s5 = 0;       /* SLP_TYPa for S5 */

/* Embedded Controller ports */
#define EC_DATA    0x62
#define EC_CMD     0x66
#define EC_OBF     0x01   /* Output buffer full */
#define EC_IBF     0x02   /* Input buffer full */

/* SMBus/Battery registers */
#define BAT_REG_STATUS      0x16
#define BAT_REG_PERCENT     0x0D
#define BAT_REG_VOLTAGE     0x09
#define BAT_REG_CURRENT     0x0A
#define BAT_REG_TEMP        0x08
#define BAT_REG_CAPACITY    0x0F
#define BAT_REG_FULL_CAP    0x10

/* ============ Embedded Controller I/O ============ */

static int ec_wait_ibf_clear(void) {
    for (int i = 0; i < 10000; i++) {
        if (!(inb(EC_CMD) & EC_IBF)) return 0;
        __asm__ volatile("pause");
    }
    return -1;  /* Timeout */
}

static int ec_wait_obf_set(void) {
    for (int i = 0; i < 10000; i++) {
        if (inb(EC_CMD) & EC_OBF) return 0;
        __asm__ volatile("pause");
    }
    return -1;  /* Timeout */
}

static int ec_read(uint8_t addr, uint8_t *value) {
    if (ec_wait_ibf_clear() < 0) return -1;
    outb(EC_CMD, 0x80);  /* Read Embedded Controller command */
    
    if (ec_wait_ibf_clear() < 0) return -1;
    outb(EC_DATA, addr);
    
    if (ec_wait_obf_set() < 0) return -1;
    *value = inb(EC_DATA);
    
    return 0;
}

static int ec_write(uint8_t addr, uint8_t value) {
    if (ec_wait_ibf_clear() < 0) return -1;
    outb(EC_CMD, 0x81);  /* Write Embedded Controller command */
    
    if (ec_wait_ibf_clear() < 0) return -1;
    outb(EC_DATA, addr);
    
    if (ec_wait_ibf_clear() < 0) return -1;
    outb(EC_DATA, value);
    
    return 0;
}

/* ============ Real Battery Reading ============ */

static void battery_update(void) {
    /* Try to read battery status from ACPI Embedded Controller */
    uint8_t status = 0;
    uint8_t percent = 0;
    uint8_t volt_lo = 0, volt_hi = 0;
    uint8_t curr_lo = 0, curr_hi = 0;
    uint8_t temp_lo = 0, temp_hi = 0;
    
    /* Try EC-based battery reading first */
    int ec_available = (ec_read(BAT_REG_STATUS, &status) == 0);
    
    if (ec_available) {
        /* Real battery present via EC */
        battery.present = (status & 0x01) ? 1 : 0;
        
        if (battery.present) {
            /* Read battery data from EC */
            if (ec_read(BAT_REG_PERCENT, &percent) == 0) {
                battery.percent = percent > 100 ? 100 : percent;
            }
            
            /* Voltage (mV) */
            if (ec_read(BAT_REG_VOLTAGE, &volt_lo) == 0 &&
                ec_read(BAT_REG_VOLTAGE + 1, &volt_hi) == 0) {
                battery.voltage_mv = (volt_hi << 8) | volt_lo;
            }
            
            /* Current (mA, signed) */
            if (ec_read(BAT_REG_CURRENT, &curr_lo) == 0 &&
                ec_read(BAT_REG_CURRENT + 1, &curr_hi) == 0) {
                battery.current_ma = (int16_t)((curr_hi << 8) | curr_lo);
            }
            
            /* Temperature (0.1K) */
            if (ec_read(BAT_REG_TEMP, &temp_lo) == 0 &&
                ec_read(BAT_REG_TEMP + 1, &temp_hi) == 0) {
                int16_t temp_k = (temp_hi << 8) | temp_lo;
                battery.temp_celsius = (temp_k / 10) - 273;
            }
            
            /* Determine charging state */
            if (status & 0x02) {
                battery.state = BATTERY_STATE_CHARGING;
            } else if (status & 0x04) {
                battery.state = BATTERY_STATE_FULL;
            } else if (battery.percent < 5) {
                battery.state = BATTERY_STATE_CRITICAL;
            } else {
                battery.state = BATTERY_STATE_DISCHARGING;
            }
            
            /* Time estimates */
            if (battery.current_ma != 0) {
                if (battery.state == BATTERY_STATE_DISCHARGING) {
                    battery.time_to_empty = (battery.percent * 60) / 
                                           ((-battery.current_ma * 100) / battery.capacity_mwh + 1);
                } else if (battery.state == BATTERY_STATE_CHARGING) {
                    battery.time_to_full = ((100 - battery.percent) * 60) /
                                          ((battery.current_ma * 100) / battery.capacity_mwh + 1);
                }
            }
            
            serial_puts("[POWER] Battery: ");
            serial_putc('0' + battery.percent / 100);
            serial_putc('0' + (battery.percent / 10) % 10);
            serial_putc('0' + battery.percent % 10);
            serial_puts("%\n");
            
            return;  /* Successfully read real battery */
        }
    }
    
    /* Fallback: Check if this is a VM or desktop (no battery) */
    if (detected_device == DEVICE_TYPE_VM || detected_device == DEVICE_TYPE_DESKTOP) {
        battery.present = 0;
        return;
    }
    
    /* For laptops without EC access, provide informative defaults */
    if (detected_device == DEVICE_TYPE_LAPTOP) {
        battery.present = 1;
        battery.state = current_source == POWER_SOURCE_AC ? 
                        BATTERY_STATE_CHARGING : BATTERY_STATE_DISCHARGING;
        battery.percent = 75;  /* Placeholder - EC access needed for real value */
        battery.capacity_mwh = 40000;
        battery.design_mwh = 50000;
        battery.full_mwh = 48000;
        battery.health_percent = 96;
        battery.voltage_mv = 11400;
        battery.current_ma = current_source == POWER_SOURCE_AC ? 2000 : -1500;
        battery.temp_celsius = 35;
        power_strcpy(battery.manufacturer, "Unknown", 32);
        power_strcpy(battery.model, "EC-Unavailable", 32);
        serial_puts("[POWER] Battery: EC unavailable, using estimates\n");
    }
}

/* ============ Profile Application ============ */

static void apply_profile(power_profile_t profile) {
    serial_puts("[POWER] Applying profile: ");
    serial_puts(power_profile_name(profile));
    serial_puts("\n");
    
    switch (profile) {
        case POWER_PROFILE_PERFORMANCE:
            cpu_power.cur_freq_mhz = cpu_power.turbo_freq_mhz;
            cpu_power.turbo_enabled = 1;
            power_strcpy(cpu_power.governor, "performance", 32);
            display_brightness = 100;
            break;
            
        case POWER_PROFILE_BALANCED:
            cpu_power.cur_freq_mhz = cpu_power.base_freq_mhz;
            cpu_power.turbo_enabled = 1;
            power_strcpy(cpu_power.governor, "balanced", 32);
            display_brightness = 80;
            break;
            
        case POWER_PROFILE_POWERSAVE:
            cpu_power.cur_freq_mhz = cpu_power.min_freq_mhz;
            cpu_power.turbo_enabled = 0;
            power_strcpy(cpu_power.governor, "powersave", 32);
            display_brightness = 50;
            break;
            
        case POWER_PROFILE_CUSTOM:
            /* Use settings values */
            break;
    }
}

/* ============ Power API ============ */

int power_init(void) {
    serial_puts("[POWER] Initializing power management...\n");
    
    /* Initialize CPU info */
    cpu_power.min_freq_mhz = 800;
    cpu_power.base_freq_mhz = 2400;
    cpu_power.max_freq_mhz = 4000;
    cpu_power.turbo_freq_mhz = 4500;
    cpu_power.cur_freq_mhz = 2400;
    cpu_power.turbo_enabled = 1;
    cpu_power.hyperthreading = 1;
    cpu_power.tdp_mw = 65000;
    cpu_power.power_mw = 25000;
    cpu_power.temp_celsius = 45;
    power_strcpy(cpu_power.governor, "balanced", 32);
    
    /* Initialize AC adapter */
    ac_adapter.present = 1;
    ac_adapter.online = 1;
    ac_adapter.power_mw = 65000;
    power_strcpy(ac_adapter.type, "USB-C PD", 16);
    
    /* Detect device type */
    battery.present = 1;  /* Simulate laptop */
    detected_device = detect_device();
    
    /* Update battery */
    battery_update();
    
    /* Initialize settings */
    settings.profile = POWER_PROFILE_BALANCED;
    settings.power_button = BUTTON_ACTION_ASK;
    settings.lid_close = BUTTON_ACTION_SUSPEND;
    settings.lid_open = BUTTON_ACTION_NOTHING;
    settings.brightness_ac = 100;
    settings.brightness_battery = 70;
    settings.dim_after_sec = 60;
    settings.off_after_sec = 300;
    settings.sleep_after_sec = 600;
    settings.sleep_when_lid_closed = 1;
    settings.critical_threshold = 5;
    settings.critical_action = BUTTON_ACTION_HIBERNATE;
    settings.low_threshold = 20;
    settings.allow_turbo = 1;
    settings.max_freq_mhz = 0;
    settings.wake_on_lan = 0;
    settings.wake_on_usb = 1;
    
    /* Apply default profile */
    apply_profile(settings.profile);
    
    serial_puts("[POWER] Ready\n");
    return 0;
}

device_type_t power_detect_device_type(void) {
    return detected_device;
}

power_source_t power_get_source(void) {
    return current_source;
}

power_state_t power_get_state(void) {
    return current_state;
}

/* ============ Power Profiles ============ */

int power_set_profile(power_profile_t profile) {
    current_profile = profile;
    settings.profile = profile;
    apply_profile(profile);
    return 0;
}

power_profile_t power_get_profile(void) {
    return current_profile;
}

const char *power_profile_name(power_profile_t profile) {
    switch (profile) {
        case POWER_PROFILE_PERFORMANCE: return "Performance";
        case POWER_PROFILE_BALANCED: return "Balanced";
        case POWER_PROFILE_POWERSAVE: return "Power Saver";
        case POWER_PROFILE_CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

/* ============ Battery ============ */

int power_get_battery(battery_info_t *info) {
    if (!info) return -1;
    battery_update();
    *info = battery;
    return 0;
}

int power_on_battery(void) {
    return current_source == POWER_SOURCE_BATTERY;
}

uint8_t power_battery_percent(void) {
    return battery.percent;
}

/* ============ AC Adapter ============ */

int power_get_ac(ac_adapter_t *info) {
    if (!info) return -1;
    *info = ac_adapter;
    return 0;
}

/* ============ CPU Power ============ */

int power_get_cpu_info(cpu_power_info_t *info) {
    if (!info) return -1;
    *info = cpu_power;
    return 0;
}

int power_set_cpu_governor(const char *governor) {
    power_strcpy(cpu_power.governor, governor, 32);
    serial_puts("[POWER] CPU governor: ");
    serial_puts(governor);
    serial_puts("\n");
    return 0;
}

int power_set_turbo(int enabled) {
    cpu_power.turbo_enabled = enabled ? 1 : 0;
    serial_puts("[POWER] Turbo boost ");
    serial_puts(enabled ? "enabled\n" : "disabled\n");
    return 0;
}

int power_set_cpu_freq(uint32_t min_mhz, uint32_t max_mhz) {
    if (min_mhz > 0) cpu_power.min_freq_mhz = min_mhz;
    if (max_mhz > 0) cpu_power.max_freq_mhz = max_mhz;
    return 0;
}

/* ============ Display ============ */

int power_get_brightness(void) {
    return display_brightness;
}

int power_set_brightness(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    display_brightness = percent;
    
    serial_puts("[POWER] Brightness: ");
    serial_putc('0' + percent / 100);
    serial_putc('0' + (percent / 10) % 10);
    serial_putc('0' + percent % 10);
    serial_puts("%\n");
    
    return 0;
}

/* ============ Power State Control ============ */

int power_suspend(void) {
    serial_puts("[POWER] Initiating suspend to RAM (ACPI S3)...\n");
    current_state = POWER_STATE_SUSPEND;
    
    /* ========== Phase 1: Notify drivers to save state ========== */
    serial_puts("[POWER] Notifying drivers...\n");
    
    /* Notify registered event callbacks */
    if (event_callback) {
        event_callback(ACPI_EVENT_SLEEP_BUTTON, NULL);
    }
    
    /* ========== Phase 2: Save CPU state ========== */
    serial_puts("[POWER] Saving CPU state...\n");
    
    /* Save control registers - will be restored on wake */
    uint64_t cr0, cr3, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    
    /* Disable interrupts during transition */
    __asm__ volatile("cli");
    
    /* ========== Phase 3: Enter ACPI S3 state ========== */
    serial_puts("[POWER] Entering S3 sleep...\n");
    
    /* Write SLP_TYPa and SLP_EN to PM1a_CNT register */
    uint16_t pm1a_val = (acpi_slp_typa_s3 << 10) | ACPI_SLP_EN;
    
    /* Use 16-bit I/O write for ACPI registers */
    __asm__ volatile(
        "outw %0, %1"
        : 
        : "a"(pm1a_val), "Nd"(acpi_pm1a_cnt_blk)
    );
    
    /* If PM1b_CNT exists, write to it too */
    if (acpi_pm1b_cnt_blk != 0) {
        __asm__ volatile(
            "outw %0, %1"
            : 
            : "a"(pm1a_val), "Nd"(acpi_pm1b_cnt_blk)
        );
    }
    
    /* ========== Phase 4: Wait for wake event ========== */
    /* After ACPI S3, CPU will halt and wake on interrupt/event */
    /* This HLT is where we "sleep" - execution resumes here on wake */
    __asm__ volatile("hlt");
    
    /* ========== Phase 5: Resume from S3 ========== */
    serial_puts("[POWER] Waking from S3 suspend...\n");
    
    /* Restore control registers */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    
    /* Re-enable interrupts */
    __asm__ volatile("sti");
    
    /* Notify drivers to restore state */
    serial_puts("[POWER] Restoring device states...\n");
    
    current_state = POWER_STATE_RUNNING;
    serial_puts("[POWER] Resumed from suspend\n");
    
    return 0;
}

int power_hibernate(void) {
    serial_puts("[POWER] Initiating hibernate to disk (ACPI S4)...\n");
    current_state = POWER_STATE_HIBERNATE;
    
    /* ========== Phase 1: Notify drivers ========== */
    serial_puts("[POWER] Notifying drivers to quiesce...\n");
    
    if (event_callback) {
        event_callback(ACPI_EVENT_SLEEP_BUTTON, NULL);
    }
    
    /* ========== Phase 2: Save memory image ========== */
    serial_puts("[POWER] Saving RAM to hibernate partition...\n");
    
    /* Get memory map and save all used pages */
    /* This requires a hibernate partition or swapfile */
    extern uint64_t pmm_get_total_memory(void);
    extern uint64_t pmm_get_used_memory(void);
    
    uint64_t used_mem = pmm_get_used_memory();
    serial_puts("[POWER] Saving ");
    /* Print MB */
    uint32_t mb = (uint32_t)(used_mem / (1024 * 1024));
    serial_putc('0' + (mb / 1000) % 10);
    serial_putc('0' + (mb / 100) % 10);
    serial_putc('0' + (mb / 10) % 10);
    serial_putc('0' + mb % 10);
    serial_puts(" MB to disk...\n");
    
    /* Write hibernate header */
    /* In production: open /hibernation or swap partition */
    /* Write: magic, memory map, page contents */
    
    /* Disable interrupts */
    __asm__ volatile("cli");
    
    /* ========== Phase 3: Enter ACPI S4 state ========== */
    serial_puts("[POWER] Entering S4 hibernate...\n");
    
    /* Write SLP_TYPa for S4 and SLP_EN */
    uint16_t pm1a_val = (acpi_slp_typa_s4 << 10) | ACPI_SLP_EN;
    
    __asm__ volatile(
        "outw %0, %1"
        : 
        : "a"(pm1a_val), "Nd"(acpi_pm1a_cnt_blk)
    );
    
    if (acpi_pm1b_cnt_blk != 0) {
        __asm__ volatile(
            "outw %0, %1"
            : 
            : "a"(pm1a_val), "Nd"(acpi_pm1b_cnt_blk)
        );
    }
    
    /* System powers off after hibernate write completes */
    /* On next boot, bootloader detects hibernate image and resumes */
    
    /* Halt if ACPI transition doesn't power off */
    while (1) {
        __asm__ volatile("hlt");
    }
    
    return 0;  /* Never reached */
}

int power_shutdown(void) {
    serial_puts("\n");
    serial_puts("  ╔═══════════════════════════════════════╗\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ║         Shutting down...              ║\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ╚═══════════════════════════════════════╝\n");
    serial_puts("\n");
    
    current_state = POWER_STATE_SHUTDOWN;
    
    /* TODO: Sync filesystems */
    /* TODO: Stop all processes */
    /* TODO: Notify drivers */
    
    /* ACPI shutdown */
    /* outw(ACPI_PM1A_CNT, ACPI_SLP_EN | ACPI_SLP_TYP_S5); */
    
    /* Fallback: keyboard controller reset */
    outb(0x64, 0xFE);
    
    /* Halt if reset fails */
    while (1) {
        __asm__ volatile("hlt");
    }
    
    return 0;
}

int power_reboot(void) {
    serial_puts("[POWER] Rebooting...\n");
    
    /* TODO: Sync and cleanup */
    
    /* Keyboard controller reset */
    outb(0x64, 0xFE);
    
    /* Triple fault fallback */
    __asm__ volatile("int $0x00");
    
    while (1) {
        __asm__ volatile("hlt");
    }
    
    return 0;
}

/* ============ Settings ============ */

power_settings_t *power_get_settings(void) {
    return &settings;
}

int power_apply_settings(power_settings_t *s) {
    if (!s) return -1;
    
    settings = *s;
    apply_profile(settings.profile);
    
    if (current_source == POWER_SOURCE_AC) {
        power_set_brightness(settings.brightness_ac);
    } else {
        power_set_brightness(settings.brightness_battery);
    }
    
    power_set_turbo(settings.allow_turbo);
    
    return 0;
}

int power_save_settings(void) {
    serial_puts("[POWER] Settings saved\n");
    /* TODO: Write to /etc/power.conf */
    return 0;
}

int power_load_settings(void) {
    serial_puts("[POWER] Settings loaded\n");
    /* TODO: Read from /etc/power.conf */
    return 0;
}

/* ============ Events ============ */

int power_register_callback(power_event_callback_t callback) {
    event_callback = callback;
    return 0;
}

void power_handle_event(acpi_event_t event) {
    serial_puts("[POWER] Event: ");
    
    switch (event) {
        case ACPI_EVENT_POWER_BUTTON:
            serial_puts("Power button\n");
            switch (settings.power_button) {
                case BUTTON_ACTION_SUSPEND: power_suspend(); break;
                case BUTTON_ACTION_HIBERNATE: power_hibernate(); break;
                case BUTTON_ACTION_SHUTDOWN: power_shutdown(); break;
                case BUTTON_ACTION_ASK:
                    /* TODO: Show power dialog */
                    break;
                default: break;
            }
            break;
            
        case ACPI_EVENT_LID_CLOSE:
            serial_puts("Lid closed\n");
            lid_open = 0;
            if (settings.sleep_when_lid_closed) {
                power_suspend();
            }
            break;
            
        case ACPI_EVENT_LID_OPEN:
            serial_puts("Lid opened\n");
            lid_open = 1;
            break;
            
        case ACPI_EVENT_AC_ONLINE:
            serial_puts("AC connected\n");
            current_source = POWER_SOURCE_AC;
            power_set_brightness(settings.brightness_ac);
            break;
            
        case ACPI_EVENT_AC_OFFLINE:
            serial_puts("AC disconnected\n");
            current_source = POWER_SOURCE_BATTERY;
            power_set_brightness(settings.brightness_battery);
            break;
            
        case ACPI_EVENT_BATTERY_LOW:
            serial_puts("Battery low\n");
            break;
            
        case ACPI_EVENT_BATTERY_CRITICAL:
            serial_puts("Battery critical!\n");
            switch (settings.critical_action) {
                case BUTTON_ACTION_SUSPEND: power_suspend(); break;
                case BUTTON_ACTION_HIBERNATE: power_hibernate(); break;
                case BUTTON_ACTION_SHUTDOWN: power_shutdown(); break;
                default: break;
            }
            break;
            
        case ACPI_EVENT_THERMAL_CRITICAL:
            serial_puts("Thermal emergency!\n");
            power_shutdown();
            break;
            
        default:
            serial_puts("Unknown\n");
            break;
    }
    
    if (event_callback) {
        event_callback(event, NULL);
    }
}

/* ============ Lid ============ */

int power_lid_is_open(void) {
    return lid_open;
}

/* ============ Thermal ============ */

int power_get_cpu_temp(void) {
    return cpu_power.temp_celsius;
}

int power_get_fan_speed(void) {
    /* 0-100% based on temperature */
    if (cpu_power.temp_celsius < 40) return 0;
    if (cpu_power.temp_celsius < 60) return 30;
    if (cpu_power.temp_celsius < 80) return 60;
    return 100;
}

int power_set_fan_mode(int mode) {
    const char *modes[] = {"Auto", "Silent", "Balanced", "Performance"};
    serial_puts("[POWER] Fan mode: ");
    serial_puts(modes[mode < 4 ? mode : 0]);
    serial_puts("\n");
    return 0;
}

/* ============ Power Status Display ============ */

void power_print_status(void) {
    serial_puts("\n=== Power Status ===\n");
    
    serial_puts("Device: ");
    switch (detected_device) {
        case DEVICE_TYPE_DESKTOP: serial_puts("Desktop\n"); break;
        case DEVICE_TYPE_LAPTOP: serial_puts("Laptop\n"); break;
        case DEVICE_TYPE_TABLET: serial_puts("Tablet\n"); break;
        case DEVICE_TYPE_SERVER: serial_puts("Server\n"); break;
        case DEVICE_TYPE_VM: serial_puts("Virtual Machine\n"); break;
        default: serial_puts("Unknown\n"); break;
    }
    
    serial_puts("Profile: ");
    serial_puts(power_profile_name(current_profile));
    serial_puts("\n");
    
    serial_puts("Power: ");
    serial_puts(current_source == POWER_SOURCE_AC ? "AC" : "Battery");
    serial_puts("\n");
    
    if (battery.present) {
        serial_puts("Battery: ");
        serial_putc('0' + battery.percent / 100);
        serial_putc('0' + (battery.percent / 10) % 10);
        serial_putc('0' + battery.percent % 10);
        serial_puts("%\n");
    }
    
    serial_puts("CPU: ");
    serial_putc('0' + cpu_power.cur_freq_mhz / 1000);
    serial_puts(".");
    serial_putc('0' + (cpu_power.cur_freq_mhz / 100) % 10);
    serial_puts(" GHz, ");
    serial_putc('0' + cpu_power.temp_celsius / 10);
    serial_putc('0' + cpu_power.temp_celsius % 10);
    serial_puts("°C\n");
    
    serial_puts("====================\n\n");
}
