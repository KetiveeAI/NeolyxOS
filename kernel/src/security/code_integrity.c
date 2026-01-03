/*
 * NeolyxOS Code Integrity Verification
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 * 
 * Binary signature validation system inspired by macOS AMFI.
 * Verifies executable trust before loading/execution.
 */

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);
extern void *memset(void *s, int c, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

/*
 * Trust levels for binaries
 */
typedef enum {
    TRUST_SYSTEM = 0,
    TRUST_DEVELOPER = 1,
    TRUST_UNTRUSTED = 2,
    TRUST_REVOKED = 3
} code_trust_level_t;

typedef enum {
    CI_MODE_DISABLED = 0,
    CI_MODE_AUDIT = 1,
    CI_MODE_ENFORCE = 2
} ci_enforce_mode_t;

typedef struct {
    bool valid;
    code_trust_level_t trust;
    uint32_t flags;
    uint8_t hash[32];
} ci_result_t;

#define CI_FLAG_SYSTEM      (1 << 0)
#define CI_FLAG_HARDENED    (1 << 1)
#define CI_FLAG_LIBRARY     (1 << 2)
#define CI_FLAG_RUNTIME     (1 << 3)

/*
 * Hash database entry
 */
typedef struct {
    uint8_t hash[32];
    code_trust_level_t trust;
    uint32_t flags;
    bool in_use;
} ci_hash_entry_t;

#define CI_MAX_HASHES 256

/* State */
static ci_hash_entry_t g_hash_db[CI_MAX_HASHES];
static ci_enforce_mode_t g_mode = CI_MODE_AUDIT;
static bool g_initialized = false;
static uint32_t g_check_count = 0;
static uint32_t g_deny_count = 0;

/*
 * Simple SHA-256-like hash (simplified for kernel use)
 * In production, use real cryptographic implementation
 */
static void compute_hash(const void *data, size_t size, uint8_t hash_out[32])
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    
    /* Simple mixing based on data */
    for (size_t i = 0; i < size; i++) {
        uint32_t b = bytes[i];
        state[i % 8] ^= (b << ((i * 7) % 24));
        state[(i + 1) % 8] += state[i % 8] >> 3;
        state[(i + 2) % 8] ^= (state[i % 8] << 5);
    }
    
    /* Mix rounds */
    for (int round = 0; round < 64; round++) {
        uint32_t t = state[0];
        for (int j = 0; j < 7; j++) {
            state[j] = state[j + 1];
        }
        state[7] = t ^ (state[3] >> 11) ^ (state[5] << 7);
    }
    
    /* Output */
    for (int i = 0; i < 8; i++) {
        hash_out[i * 4 + 0] = (state[i] >> 24) & 0xFF;
        hash_out[i * 4 + 1] = (state[i] >> 16) & 0xFF;
        hash_out[i * 4 + 2] = (state[i] >> 8) & 0xFF;
        hash_out[i * 4 + 3] = state[i] & 0xFF;
    }
}

static void print_hex_byte(uint8_t b)
{
    const char hex[] = "0123456789abcdef";
    serial_putc(hex[(b >> 4) & 0xF]);
    serial_putc(hex[b & 0xF]);
}

/*
 * Initialize code integrity subsystem
 */
void code_integrity_init(void)
{
    if (g_initialized) {
        return;
    }
    
    /* Clear hash database */
    memset(g_hash_db, 0, sizeof(g_hash_db));
    
    g_mode = CI_MODE_AUDIT;  /* Start in audit mode */
    g_check_count = 0;
    g_deny_count = 0;
    g_initialized = true;
    
    serial_puts("[CODE_INTEGRITY] Subsystem initialized (audit mode)\n");
}

/*
 * Set enforcement mode
 */
void code_integrity_set_mode(ci_enforce_mode_t mode)
{
    g_mode = mode;
    
    serial_puts("[CODE_INTEGRITY] Mode set to: ");
    switch (mode) {
        case CI_MODE_DISABLED:
            serial_puts("DISABLED\n");
            break;
        case CI_MODE_AUDIT:
            serial_puts("AUDIT\n");
            break;
        case CI_MODE_ENFORCE:
            serial_puts("ENFORCE\n");
            break;
    }
}

/*
 * Get current enforcement mode
 */
ci_enforce_mode_t code_integrity_get_mode(void)
{
    return g_mode;
}

/*
 * Find hash in database
 */
static ci_hash_entry_t *find_hash(const uint8_t hash[32])
{
    for (int i = 0; i < CI_MAX_HASHES; i++) {
        if (g_hash_db[i].in_use) {
            if (memcmp(g_hash_db[i].hash, hash, 32) == 0) {
                return &g_hash_db[i];
            }
        }
    }
    return NULL;
}

/*
 * Check binary before execution
 */
ci_result_t code_integrity_check(const void *binary, size_t size)
{
    ci_result_t result;
    memset(&result, 0, sizeof(result));
    
    if (!g_initialized) {
        code_integrity_init();
    }
    
    if (g_mode == CI_MODE_DISABLED) {
        result.valid = true;
        result.trust = TRUST_UNTRUSTED;
        return result;
    }
    
    g_check_count++;
    
    /* Validate input */
    if (binary == NULL || size == 0) {
        result.valid = false;
        result.trust = TRUST_UNTRUSTED;
        return result;
    }
    
    /* Compute hash */
    compute_hash(binary, size, result.hash);
    
    /* Look up in database */
    ci_hash_entry_t *entry = find_hash(result.hash);
    
    if (entry != NULL) {
        if (entry->trust == TRUST_REVOKED) {
            result.valid = false;
            result.trust = TRUST_REVOKED;
            result.flags = entry->flags;
            
            serial_puts("[CODE_INTEGRITY] REVOKED binary blocked\n");
            g_deny_count++;
        } else {
            result.valid = true;
            result.trust = entry->trust;
            result.flags = entry->flags;
        }
    } else {
        /* Unknown binary */
        result.trust = TRUST_UNTRUSTED;
        
        if (g_mode == CI_MODE_ENFORCE) {
            result.valid = false;
            serial_puts("[CODE_INTEGRITY] Unsigned binary blocked\n");
            g_deny_count++;
        } else {
            result.valid = true;
            serial_puts("[CODE_INTEGRITY] Unsigned binary allowed (audit)\n");
        }
    }
    
    return result;
}

/*
 * Register a trusted binary hash
 */
int code_integrity_register(const uint8_t hash[32], code_trust_level_t trust, uint32_t flags)
{
    if (!g_initialized) {
        code_integrity_init();
    }
    
    /* Check if already registered */
    ci_hash_entry_t *existing = find_hash(hash);
    if (existing != NULL) {
        existing->trust = trust;
        existing->flags = flags;
        return 0;
    }
    
    /* Find empty slot */
    for (int i = 0; i < CI_MAX_HASHES; i++) {
        if (!g_hash_db[i].in_use) {
            memcpy(g_hash_db[i].hash, hash, 32);
            g_hash_db[i].trust = trust;
            g_hash_db[i].flags = flags;
            g_hash_db[i].in_use = true;
            
            serial_puts("[CODE_INTEGRITY] Binary registered: ");
            for (int j = 0; j < 4; j++) {
                print_hex_byte(hash[j]);
            }
            serial_puts("...\n");
            
            return 0;
        }
    }
    
    serial_puts("[CODE_INTEGRITY] Hash database full\n");
    return -1;
}

/*
 * Revoke a binary hash
 */
int code_integrity_revoke(const uint8_t hash[32])
{
    ci_hash_entry_t *entry = find_hash(hash);
    if (entry == NULL) {
        return -1;
    }
    
    entry->trust = TRUST_REVOKED;
    
    serial_puts("[CODE_INTEGRITY] Binary revoked: ");
    for (int i = 0; i < 4; i++) {
        print_hex_byte(hash[i]);
    }
    serial_puts("...\n");
    
    return 0;
}

/*
 * Check if hash is trusted
 */
bool code_integrity_is_trusted(const uint8_t hash[32])
{
    ci_hash_entry_t *entry = find_hash(hash);
    if (entry == NULL) {
        return false;
    }
    return entry->trust == TRUST_SYSTEM || entry->trust == TRUST_DEVELOPER;
}

/*
 * Get trust level for hash
 */
code_trust_level_t code_integrity_get_trust(const uint8_t hash[32])
{
    ci_hash_entry_t *entry = find_hash(hash);
    if (entry == NULL) {
        return TRUST_UNTRUSTED;
    }
    return entry->trust;
}

/*
 * Print decimal number
 */
static void print_number(uint32_t n)
{
    char buf[16];
    int i = 0;
    
    if (n == 0) {
        serial_putc('0');
        return;
    }
    
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

/*
 * Display status
 */
void code_integrity_status(void)
{
    serial_puts("\n=== Code Integrity Status ===\n\n");
    
    serial_puts("Mode: ");
    switch (g_mode) {
        case CI_MODE_DISABLED:
            serial_puts("DISABLED\n");
            break;
        case CI_MODE_AUDIT:
            serial_puts("AUDIT (log only)\n");
            break;
        case CI_MODE_ENFORCE:
            serial_puts("ENFORCE (block unsigned)\n");
            break;
    }
    
    serial_puts("Initialized: ");
    serial_puts(g_initialized ? "Yes\n" : "No\n");
    
    serial_puts("Checks performed: ");
    print_number(g_check_count);
    serial_puts("\n");
    
    serial_puts("Denials: ");
    print_number(g_deny_count);
    serial_puts("\n");
    
    /* Count registered hashes */
    int count = 0;
    int system_count = 0;
    int dev_count = 0;
    int revoked_count = 0;
    
    for (int i = 0; i < CI_MAX_HASHES; i++) {
        if (g_hash_db[i].in_use) {
            count++;
            switch (g_hash_db[i].trust) {
                case TRUST_SYSTEM: system_count++; break;
                case TRUST_DEVELOPER: dev_count++; break;
                case TRUST_REVOKED: revoked_count++; break;
                default: break;
            }
        }
    }
    
    serial_puts("Registered hashes: ");
    print_number(count);
    serial_puts(" (system: ");
    print_number(system_count);
    serial_puts(", developer: ");
    print_number(dev_count);
    serial_puts(", revoked: ");
    print_number(revoked_count);
    serial_puts(")\n\n");
}
