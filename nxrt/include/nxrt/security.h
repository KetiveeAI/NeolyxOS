/*
 * NXRT Security Module
 * 
 * Security primitives for NeolyxOS Application Runtime:
 * - Input validation (paths, URLs, strings)
 * - Secure memory handling
 * - Sandbox enforcement
 * - Permission validation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXRT_SECURITY_H
#define NXRT_SECURITY_H

#include "nxrt/nxrt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============ Security Error Codes ============ */
typedef enum {
    NXRT_SEC_OK              = 0,
    NXRT_SEC_INVALID_INPUT   = -100,
    NXRT_SEC_PATH_TRAVERSAL  = -101,
    NXRT_SEC_INVALID_URL     = -102,
    NXRT_SEC_SANDBOX_VIOLATION = -103,
    NXRT_SEC_PERMISSION_DENIED = -104,
    NXRT_SEC_BUFFER_OVERFLOW = -105,
    NXRT_SEC_ENCRYPTION_FAILED = -106,
    NXRT_SEC_DECRYPTION_FAILED = -107,
} nxrt_security_error_t;

/* ============ Validation Flags ============ */
typedef enum {
    NXRT_VALIDATE_NONE       = 0,
    NXRT_VALIDATE_NO_NULL    = (1 << 0),  /* No null bytes */
    NXRT_VALIDATE_NO_CONTROL = (1 << 1),  /* No control characters */
    NXRT_VALIDATE_ASCII      = (1 << 2),  /* ASCII only */
    NXRT_VALIDATE_UTF8       = (1 << 3),  /* Valid UTF-8 */
    NXRT_VALIDATE_PRINTABLE  = (1 << 4),  /* Printable only */
    NXRT_VALIDATE_ALPHANUMERIC = (1 << 5), /* [A-Za-z0-9_-] */
} nxrt_validate_flags_t;

/* ============ URL Schemes ============ */
typedef enum {
    NXRT_URL_ALLOW_HTTP      = (1 << 0),
    NXRT_URL_ALLOW_HTTPS     = (1 << 1),
    NXRT_URL_ALLOW_FILE      = (1 << 2),
    NXRT_URL_ALLOW_NXRT      = (1 << 3),  /* nxrt:// scheme */
    NXRT_URL_ALLOW_DATA      = (1 << 4),
    NXRT_URL_ALLOW_BLOB      = (1 << 5),
    NXRT_URL_ALLOW_SAFE      = (NXRT_URL_ALLOW_HTTPS | NXRT_URL_ALLOW_NXRT),
    NXRT_URL_ALLOW_ALL       = 0xFFFF,
} nxrt_url_scheme_t;

/* ===========================================================================
 * Input Validation API
 * =========================================================================*/

/**
 * Validate string input
 * @param str String to validate
 * @param max_len Maximum allowed length
 * @param flags Validation flags
 * @return NXRT_SEC_OK or error code
 */
nxrt_security_error_t nxrt_validate_string(
    const char *str,
    size_t max_len,
    uint32_t flags
);

/**
 * Validate file path - prevents path traversal attacks
 * @param path Path to validate
 * @param base_dir Allowed base directory (sandbox root)
 * @param resolved_path Buffer for resolved absolute path
 * @param resolved_size Size of resolved_path buffer
 * @return NXRT_SEC_OK or error code
 */
nxrt_security_error_t nxrt_validate_path(
    const char *path,
    const char *base_dir,
    char *resolved_path,
    size_t resolved_size
);

/**
 * Validate URL
 * @param url URL to validate
 * @param allowed_schemes Bitmask of allowed schemes
 * @return NXRT_SEC_OK or error code
 */
nxrt_security_error_t nxrt_validate_url(
    const char *url,
    uint32_t allowed_schemes
);

/**
 * Sanitize HTML content - escape dangerous characters
 * @param input Input string
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return Bytes written or -1 on error
 */
int nxrt_sanitize_html(
    const char *input,
    char *output,
    size_t output_size
);

/**
 * Validate JSON structure
 * @param json JSON string
 * @param max_depth Maximum nesting depth
 * @return NXRT_SEC_OK or error code
 */
nxrt_security_error_t nxrt_validate_json(
    const char *json,
    int max_depth
);

/* ===========================================================================
 * Secure Memory API
 * =========================================================================*/

/**
 * Allocate secure memory (will be zeroed on free)
 * @param size Bytes to allocate
 * @return Pointer or NULL
 */
void* nxrt_secure_alloc(size_t size);

/**
 * Reallocate secure memory
 * @param ptr Existing pointer
 * @param old_size Previous size
 * @param new_size New size
 * @return New pointer or NULL
 */
void* nxrt_secure_realloc(void *ptr, size_t old_size, size_t new_size);

/**
 * Free secure memory (zeros before freeing)
 * @param ptr Pointer to free
 * @param size Size of allocation
 */
void nxrt_secure_free(void *ptr, size_t size);

/**
 * Securely zero memory (not optimized away)
 * @param ptr Memory to zero
 * @param size Bytes to zero
 */
void nxrt_secure_zero(void *ptr, size_t size);

/**
 * Constant-time memory comparison
 * @param a First buffer
 * @param b Second buffer
 * @param size Bytes to compare
 * @return 0 if equal, non-zero otherwise
 */
int nxrt_secure_compare(const void *a, const void *b, size_t size);

/* ===========================================================================
 * Sandbox API
 * =========================================================================*/

/**
 * Sandbox context for an app
 */
typedef struct nxrt_sandbox nxrt_sandbox_t;

/**
 * Create sandbox for app
 * @param app_id App to sandbox
 * @param root_dir Sandbox root directory
 * @return Sandbox handle or NULL
 */
nxrt_sandbox_t* nxrt_sandbox_create(nxrt_app_t app_id, const char *root_dir);

/**
 * Destroy sandbox
 * @param sandbox Sandbox to destroy
 */
void nxrt_sandbox_destroy(nxrt_sandbox_t *sandbox);

/**
 * Check if path is within sandbox
 * @param sandbox Sandbox context
 * @param path Path to check
 * @return 1 if allowed, 0 if violation
 */
int nxrt_sandbox_check_path(nxrt_sandbox_t *sandbox, const char *path);

/**
 * Check if IPC channel is allowed
 * @param sandbox Sandbox context
 * @param channel Channel name
 * @return 1 if allowed, 0 if violation
 */
int nxrt_sandbox_check_ipc(nxrt_sandbox_t *sandbox, const char *channel);

/**
 * Add allowed path pattern to sandbox
 * @param sandbox Sandbox context
 * @param pattern Glob pattern
 */
void nxrt_sandbox_allow_path(nxrt_sandbox_t *sandbox, const char *pattern);

/**
 * Add allowed IPC channel to sandbox
 * @param sandbox Sandbox context
 * @param channel Channel name pattern
 */
void nxrt_sandbox_allow_ipc(nxrt_sandbox_t *sandbox, const char *channel);

/* ===========================================================================
 * Permission Enforcement API
 * =========================================================================*/

/**
 * Enforce permission check (logs violation if denied)
 * @param app_id App to check
 * @param perm Permission required
 * @param operation Description for logging
 * @return NXRT_SEC_OK or NXRT_SEC_PERMISSION_DENIED
 */
nxrt_security_error_t nxrt_enforce_permission(
    nxrt_app_t app_id,
    nxrt_permission_t perm,
    const char *operation
);

/**
 * Log security event
 * @param level 0=info, 1=warning, 2=error
 * @param app_id Related app (or -1)
 * @param event Event description
 */
void nxrt_security_log(int level, nxrt_app_t app_id, const char *event);

/* ===========================================================================
 * Encryption API
 * =========================================================================*/

/**
 * Encryption context
 */
typedef struct nxrt_crypto nxrt_crypto_t;

/**
 * Create encryption context with key
 * @param key Key data
 * @param key_size Key size in bytes (16, 24, or 32)
 * @return Crypto context or NULL
 */
nxrt_crypto_t* nxrt_crypto_create(const uint8_t *key, size_t key_size);

/**
 * Destroy crypto context
 */
void nxrt_crypto_destroy(nxrt_crypto_t *ctx);

/**
 * Encrypt data (AES-256-GCM)
 * @param ctx Crypto context
 * @param plaintext Input data
 * @param plaintext_size Input size
 * @param ciphertext Output buffer (plaintext_size + 16 + 12)
 * @param ciphertext_size Output buffer size, updated with written size
 * @return NXRT_SEC_OK or error
 */
nxrt_security_error_t nxrt_crypto_encrypt(
    nxrt_crypto_t *ctx,
    const uint8_t *plaintext,
    size_t plaintext_size,
    uint8_t *ciphertext,
    size_t *ciphertext_size
);

/**
 * Decrypt data (AES-256-GCM)
 * @param ctx Crypto context
 * @param ciphertext Input data
 * @param ciphertext_size Input size
 * @param plaintext Output buffer
 * @param plaintext_size Output buffer size, updated with written size
 * @return NXRT_SEC_OK or error
 */
nxrt_security_error_t nxrt_crypto_decrypt(
    nxrt_crypto_t *ctx,
    const uint8_t *ciphertext,
    size_t ciphertext_size,
    uint8_t *plaintext,
    size_t *plaintext_size
);

/**
 * Derive key from password (PBKDF2)
 * @param password Password string
 * @param salt Salt data
 * @param salt_size Salt size
 * @param iterations PBKDF2 iterations
 * @param key Output key buffer
 * @param key_size Desired key size
 * @return NXRT_SEC_OK or error
 */
nxrt_security_error_t nxrt_crypto_derive_key(
    const char *password,
    const uint8_t *salt,
    size_t salt_size,
    int iterations,
    uint8_t *key,
    size_t key_size
);

/**
 * Generate random bytes
 * @param buffer Output buffer
 * @param size Number of bytes
 * @return NXRT_SEC_OK or error
 */
nxrt_security_error_t nxrt_crypto_random(uint8_t *buffer, size_t size);

/**
 * Hash data (SHA-256)
 * @param data Input data
 * @param data_size Input size
 * @param hash Output buffer (32 bytes)
 * @return NXRT_SEC_OK or error
 */
nxrt_security_error_t nxrt_crypto_hash(
    const uint8_t *data,
    size_t data_size,
    uint8_t hash[32]
);

#ifdef __cplusplus
}
#endif

#endif /* NXRT_SECURITY_H */
