/*
 * NeolyxOS Security Policy
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 * 
 * Provides system-wide security restrictions that can be enabled
 * to protect against various attack vectors at the cost of some
 * functionality.
 */

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
extern void serial_puts(const char *s);
extern uint64_t pit_get_ticks(void);

/*
 * Security Restriction Flags
 * Each flag controls a specific security boundary
 */
#define SEC_RESTRICT_USB            (1 << 0)  /* Block untrusted USB devices */
#define SEC_RESTRICT_MODULES        (1 << 1)  /* Block unsigned kernel modules */
#define SEC_RESTRICT_JIT            (1 << 2)  /* Disable JIT compilation */
#define SEC_RESTRICT_NETWORK        (1 << 3)  /* Enhanced network filtering */
#define SEC_RESTRICT_DEBUG          (1 << 4)  /* Block debugger attachment */
#define SEC_RESTRICT_PERIPHERALS    (1 << 5)  /* Restrict peripheral access */

#define SEC_POLICY_STRICT           0x3F      /* All restrictions enabled */
#define SEC_POLICY_STANDARD         0x00      /* Normal operation */

/* Security policy state */
typedef struct {
    uint32_t restrictions;        /* Active restriction bitmask */
    bool enabled;                 /* Policy enforcement enabled */
    uint64_t enabled_at;          /* Timestamp when enabled */
    bool initialized;             /* Subsystem initialized */
} security_policy_t;

static security_policy_t sec_policy = {
    .restrictions = 0,
    .enabled = false,
    .enabled_at = 0,
    .initialized = false
};

/*
 * Initialize security policy subsystem
 */
void security_policy_init(void)
{
    if (sec_policy.initialized) {
        return;
    }
    
    serial_puts("[SECURITY] Policy subsystem initialized\n");
    
    /* Load saved policy from config */
    sec_policy.restrictions = SEC_POLICY_STANDARD;
    sec_policy.enabled = false;
    sec_policy.initialized = true;
}

/*
 * Enable security policy with specified restrictions
 */
int security_policy_enable(uint32_t restrictions)
{
    if (!sec_policy.initialized) {
        security_policy_init();
    }
    
    if (restrictions == 0) {
        restrictions = SEC_POLICY_STRICT;
    }
    
    serial_puts("[SECURITY] Enabling security policy\n");
    
    sec_policy.restrictions = restrictions;
    sec_policy.enabled = true;
    sec_policy.enabled_at = pit_get_ticks();
    
    /* Log active restrictions */
    if (restrictions & SEC_RESTRICT_USB) {
        serial_puts("[SECURITY]   USB: restricted\n");
    }
    if (restrictions & SEC_RESTRICT_MODULES) {
        serial_puts("[SECURITY]   Kernel modules: restricted\n");
    }
    if (restrictions & SEC_RESTRICT_JIT) {
        serial_puts("[SECURITY]   JIT: disabled\n");
    }
    if (restrictions & SEC_RESTRICT_NETWORK) {
        serial_puts("[SECURITY]   Network: enhanced filtering\n");
    }
    if (restrictions & SEC_RESTRICT_DEBUG) {
        serial_puts("[SECURITY]   Debug: blocked\n");
    }
    if (restrictions & SEC_RESTRICT_PERIPHERALS) {
        serial_puts("[SECURITY]   Peripherals: restricted\n");
    }
    
    return 0;
}

/*
 * Disable security policy
 */
int security_policy_disable(void)
{
    if (!sec_policy.enabled) {
        return 0;
    }
    
    serial_puts("[SECURITY] Disabling security policy\n");
    
    sec_policy.enabled = false;
    sec_policy.restrictions = 0;
    
    return 0;
}

/*
 * Check if security policy is active
 */
bool security_policy_is_enabled(void)
{
    return sec_policy.enabled;
}

/*
 * Check if a specific restriction is active
 */
bool security_restriction_active(uint32_t restriction)
{
    if (!sec_policy.enabled) {
        return false;
    }
    return (sec_policy.restrictions & restriction) != 0;
}

/*
 * Get current restriction mask
 */
uint32_t security_get_restrictions(void)
{
    return sec_policy.restrictions;
}

/* ============ Subsystem Check APIs ============ */

/*
 * USB subsystem check
 * Called before accepting a new USB device
 * Returns true if device should be blocked
 */
bool security_check_usb(uint16_t vendor_id, uint16_t product_id)
{
    if (!security_restriction_active(SEC_RESTRICT_USB)) {
        return false;  /* Allow */
    }
    
    /* Block all USB devices in restricted mode */
    (void)vendor_id;
    (void)product_id;
    
    serial_puts("[SECURITY] USB device blocked by policy\n");
    return true;
}

/*
 * Kernel module check
 * Called before loading a kernel module
 * Returns true if module should be rejected
 */
bool security_check_module(const char *module_name, bool is_signed)
{
    if (!security_restriction_active(SEC_RESTRICT_MODULES)) {
        return false;
    }
    
    /* Allow signed modules, block unsigned */
    if (is_signed) {
        return false;  /* Allow */
    }
    
    (void)module_name;
    serial_puts("[SECURITY] Unsigned module blocked by policy\n");
    return true;
}

/*
 * JIT check
 * Called by JIT compilers to verify JIT is allowed
 * Returns true if JIT should be disabled
 */
bool security_check_jit(void)
{
    return security_restriction_active(SEC_RESTRICT_JIT);
}

/*
 * Debug check
 * Called when debugger tries to attach
 * Returns true if debug should be blocked
 */
bool security_check_debug(int target_pid)
{
    if (!security_restriction_active(SEC_RESTRICT_DEBUG)) {
        return false;
    }
    
    (void)target_pid;
    serial_puts("[SECURITY] Debugger blocked by policy\n");
    return true;
}

/*
 * Network check
 * Called for outbound connections
 * Returns true if connection should be filtered
 */
bool security_check_network(uint32_t dest_ip, uint16_t dest_port)
{
    if (!security_restriction_active(SEC_RESTRICT_NETWORK)) {
        return false;
    }
    
    /* In strict mode, block non-standard ports */
    (void)dest_ip;
    
    /* Allow standard ports */
    if (dest_port == 80 || dest_port == 443 || dest_port == 53) {
        return false;  /* Allow */
    }
    
    return true;  /* Block non-standard */
}

/*
 * Display security policy status
 */
void security_policy_status(void)
{
    serial_puts("\n=== Security Policy Status ===\n\n");
    serial_puts("Policy: ");
    serial_puts(sec_policy.enabled ? "ENABLED" : "Disabled");
    serial_puts("\n\n");
    
    if (sec_policy.enabled) {
        serial_puts("Active Restrictions:\n");
        serial_puts(sec_policy.restrictions & SEC_RESTRICT_USB ? 
                    "  [x] USB devices\n" : "  [ ] USB devices\n");
        serial_puts(sec_policy.restrictions & SEC_RESTRICT_MODULES ? 
                    "  [x] Kernel modules\n" : "  [ ] Kernel modules\n");
        serial_puts(sec_policy.restrictions & SEC_RESTRICT_JIT ? 
                    "  [x] JIT compilation\n" : "  [ ] JIT compilation\n");
        serial_puts(sec_policy.restrictions & SEC_RESTRICT_NETWORK ? 
                    "  [x] Network filtering\n" : "  [ ] Network filtering\n");
        serial_puts(sec_policy.restrictions & SEC_RESTRICT_DEBUG ? 
                    "  [x] Debugger\n" : "  [ ] Debugger\n");
        serial_puts(sec_policy.restrictions & SEC_RESTRICT_PERIPHERALS ? 
                    "  [x] Peripherals\n" : "  [ ] Peripherals\n");
    }
    
    serial_puts("\n");
}
