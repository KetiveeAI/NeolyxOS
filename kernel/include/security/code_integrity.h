/*
 * NeolyxOS Code Integrity Verification
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 * 
 * Binary signature validation system inspired by macOS AMFI.
 * Provides executable trust verification before loading.
 */

#ifndef _NEOLYX_CODE_INTEGRITY_H
#define _NEOLYX_CODE_INTEGRITY_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Trust levels for binaries
 */
typedef enum {
    TRUST_SYSTEM = 0,       /* Kernel/system binaries - full trust */
    TRUST_DEVELOPER = 1,    /* Signed by registered developer */
    TRUST_UNTRUSTED = 2,    /* Unsigned or unknown */
    TRUST_REVOKED = 3       /* Signature revoked */
} code_trust_level_t;

/*
 * Enforcement modes
 */
typedef enum {
    CI_MODE_DISABLED = 0,   /* No enforcement */
    CI_MODE_AUDIT = 1,      /* Log but allow */
    CI_MODE_ENFORCE = 2     /* Block unsigned code */
} ci_enforce_mode_t;

/*
 * Code integrity result
 */
typedef struct {
    bool valid;             /* Signature valid */
    code_trust_level_t trust;
    uint32_t flags;
    uint8_t hash[32];       /* SHA-256 hash */
} ci_result_t;

/*
 * Hash algorithm identifiers
 */
#define CI_HASH_SHA256      0x01
#define CI_HASH_SHA384      0x02

/*
 * Registration flags
 */
#define CI_FLAG_SYSTEM      (1 << 0)
#define CI_FLAG_HARDENED    (1 << 1)
#define CI_FLAG_LIBRARY     (1 << 2)
#define CI_FLAG_RUNTIME     (1 << 3)

/* Initialize code integrity subsystem */
void code_integrity_init(void);

/* Set enforcement mode */
void code_integrity_set_mode(ci_enforce_mode_t mode);

/* Get current enforcement mode */
ci_enforce_mode_t code_integrity_get_mode(void);

/* Check binary before execution */
ci_result_t code_integrity_check(const void *binary, size_t size);

/* Register a trusted binary hash */
int code_integrity_register(const uint8_t hash[32], code_trust_level_t trust, uint32_t flags);

/* Revoke a binary hash */
int code_integrity_revoke(const uint8_t hash[32]);

/* Check if hash is trusted */
bool code_integrity_is_trusted(const uint8_t hash[32]);

/* Get trust level for hash */
code_trust_level_t code_integrity_get_trust(const uint8_t hash[32]);

/* Display status */
void code_integrity_status(void);

#endif /* _NEOLYX_CODE_INTEGRITY_H */
