/*
 * NXRT Security Module Implementation
 * 
 * Input validation, secure memory, sandbox enforcement.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "nxrt/security.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <time.h>

#ifdef __linux__
#include <unistd.h>
#include <sys/random.h>
#include <sys/stat.h>
#endif

/* ============ Validation Implementation ============ */

nxrt_security_error_t nxrt_validate_string(
    const char *str,
    size_t max_len,
    uint32_t flags) {
    
    if (!str) {
        return NXRT_SEC_INVALID_INPUT;
    }
    
    size_t len = 0;
    const unsigned char *p = (const unsigned char *)str;
    
    while (*p && len < max_len + 1) {
        unsigned char c = *p;
        
        /* Check for null bytes in middle */
        if ((flags & NXRT_VALIDATE_NO_NULL) && c == 0 && len > 0) {
            return NXRT_SEC_INVALID_INPUT;
        }
        
        /* Check for control characters */
        if ((flags & NXRT_VALIDATE_NO_CONTROL) && c < 32 && c != '\t' && c != '\n' && c != '\r') {
            return NXRT_SEC_INVALID_INPUT;
        }
        
        /* Check ASCII only */
        if ((flags & NXRT_VALIDATE_ASCII) && c > 127) {
            return NXRT_SEC_INVALID_INPUT;
        }
        
        /* Check printable */
        if ((flags & NXRT_VALIDATE_PRINTABLE) && !isprint(c) && !isspace(c)) {
            return NXRT_SEC_INVALID_INPUT;
        }
        
        /* Check alphanumeric */
        if ((flags & NXRT_VALIDATE_ALPHANUMERIC) && 
            !isalnum(c) && c != '_' && c != '-' && c != '.') {
            return NXRT_SEC_INVALID_INPUT;
        }
        
        p++;
        len++;
    }
    
    if (len > max_len) {
        return NXRT_SEC_BUFFER_OVERFLOW;
    }
    
    return NXRT_SEC_OK;
}

nxrt_security_error_t nxrt_validate_path(
    const char *path,
    const char *base_dir,
    char *resolved_path,
    size_t resolved_size) {
    
    if (!path || !base_dir || !resolved_path || resolved_size == 0) {
        return NXRT_SEC_INVALID_INPUT;
    }
    
    /* Check for obvious path traversal patterns */
    if (strstr(path, "..") != NULL) {
        nxrt_security_log(2, -1, "Path traversal attempt blocked");
        return NXRT_SEC_PATH_TRAVERSAL;
    }
    
    /* Check for null bytes */
    if (memchr(path, 0, strlen(path)) != NULL) {
        return NXRT_SEC_INVALID_INPUT;
    }
    
    /* Build full path */
    char full_path[PATH_MAX];
    int written = snprintf(full_path, PATH_MAX, "%s/%s", base_dir, path);
    if (written < 0 || written >= PATH_MAX) {
        return NXRT_SEC_BUFFER_OVERFLOW;
    }
    
    /* Resolve to absolute path */
    char *resolved = realpath(full_path, NULL);
    if (!resolved) {
        /* Path doesn't exist - check parent directory */
        char *last_slash = strrchr(full_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            resolved = realpath(full_path, NULL);
            *last_slash = '/';
        }
        if (!resolved) {
            /* Just validate the base_dir exists */
            resolved = realpath(base_dir, NULL);
            if (!resolved) {
                return NXRT_SEC_INVALID_INPUT;
            }
        }
    }
    
    /* Resolve base_dir */
    char *resolved_base = realpath(base_dir, NULL);
    if (!resolved_base) {
        free(resolved);
        return NXRT_SEC_INVALID_INPUT;
    }
    
    /* Check that resolved path starts with base */
    size_t base_len = strlen(resolved_base);
    int within_sandbox = (strncmp(resolved, resolved_base, base_len) == 0) &&
                         (resolved[base_len] == '/' || resolved[base_len] == '\0');
    
    if (!within_sandbox) {
        nxrt_security_log(2, -1, "Sandbox violation: path escapes sandbox");
        free(resolved);
        free(resolved_base);
        return NXRT_SEC_SANDBOX_VIOLATION;
    }
    
    /* Copy to output */
    size_t res_len = strlen(resolved);
    if (res_len >= resolved_size) {
        free(resolved);
        free(resolved_base);
        return NXRT_SEC_BUFFER_OVERFLOW;
    }
    
    strncpy(resolved_path, resolved, resolved_size - 1);
    resolved_path[resolved_size - 1] = '\0';
    
    free(resolved);
    free(resolved_base);
    
    return NXRT_SEC_OK;
}

nxrt_security_error_t nxrt_validate_url(
    const char *url,
    uint32_t allowed_schemes) {
    
    if (!url || strlen(url) == 0) {
        return NXRT_SEC_INVALID_INPUT;
    }
    
    /* Check for null bytes */
    if (memchr(url, 0, strlen(url)) != NULL) {
        return NXRT_SEC_INVALID_INPUT;
    }
    
    /* Parse scheme */
    const char *colon = strchr(url, ':');
    if (!colon) {
        return NXRT_SEC_INVALID_URL;
    }
    
    size_t scheme_len = colon - url;
    int allowed = 0;
    
    if (scheme_len == 4 && strncasecmp(url, "http", 4) == 0) {
        allowed = (allowed_schemes & NXRT_URL_ALLOW_HTTP);
    } else if (scheme_len == 5 && strncasecmp(url, "https", 5) == 0) {
        allowed = (allowed_schemes & NXRT_URL_ALLOW_HTTPS);
    } else if (scheme_len == 4 && strncasecmp(url, "file", 4) == 0) {
        allowed = (allowed_schemes & NXRT_URL_ALLOW_FILE);
    } else if (scheme_len == 4 && strncasecmp(url, "nxrt", 4) == 0) {
        allowed = (allowed_schemes & NXRT_URL_ALLOW_NXRT);
    } else if (scheme_len == 4 && strncasecmp(url, "data", 4) == 0) {
        allowed = (allowed_schemes & NXRT_URL_ALLOW_DATA);
    } else if (scheme_len == 4 && strncasecmp(url, "blob", 4) == 0) {
        allowed = (allowed_schemes & NXRT_URL_ALLOW_BLOB);
    }
    
    if (!allowed) {
        nxrt_security_log(1, -1, "URL scheme not allowed");
        return NXRT_SEC_INVALID_URL;
    }
    
    /* Check for javascript: or other dangerous schemes in data URLs */
    if ((allowed_schemes & NXRT_URL_ALLOW_DATA) && strncasecmp(url, "data:", 5) == 0) {
        if (strstr(url, "javascript") || strstr(url, "vbscript")) {
            return NXRT_SEC_INVALID_URL;
        }
    }
    
    return NXRT_SEC_OK;
}

int nxrt_sanitize_html(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return -1;
    }
    
    size_t out_idx = 0;
    const char *p = input;
    
    while (*p && out_idx < output_size - 1) {
        switch (*p) {
            case '<':
                if (out_idx + 4 < output_size) {
                    memcpy(output + out_idx, "&lt;", 4);
                    out_idx += 4;
                } else return -1;
                break;
            case '>':
                if (out_idx + 4 < output_size) {
                    memcpy(output + out_idx, "&gt;", 4);
                    out_idx += 4;
                } else return -1;
                break;
            case '&':
                if (out_idx + 5 < output_size) {
                    memcpy(output + out_idx, "&amp;", 5);
                    out_idx += 5;
                } else return -1;
                break;
            case '"':
                if (out_idx + 6 < output_size) {
                    memcpy(output + out_idx, "&quot;", 6);
                    out_idx += 6;
                } else return -1;
                break;
            case '\'':
                if (out_idx + 5 < output_size) {
                    memcpy(output + out_idx, "&#39;", 5);
                    out_idx += 5;
                } else return -1;
                break;
            default:
                output[out_idx++] = *p;
        }
        p++;
    }
    
    output[out_idx] = '\0';
    return (int)out_idx;
}

nxrt_security_error_t nxrt_validate_json(const char *json, int max_depth) {
    if (!json) return NXRT_SEC_INVALID_INPUT;
    
    int depth = 0;
    int max_seen = 0;
    int in_string = 0;
    
    for (const char *p = json; *p; p++) {
        if (in_string) {
            if (*p == '\\' && *(p+1)) {
                p++;  /* Skip escaped char */
            } else if (*p == '"') {
                in_string = 0;
            }
        } else {
            if (*p == '"') {
                in_string = 1;
            } else if (*p == '{' || *p == '[') {
                depth++;
                if (depth > max_seen) max_seen = depth;
                if (depth > max_depth) {
                    return NXRT_SEC_BUFFER_OVERFLOW;
                }
            } else if (*p == '}' || *p == ']') {
                depth--;
                if (depth < 0) {
                    return NXRT_SEC_INVALID_INPUT;
                }
            }
        }
    }
    
    if (depth != 0 || in_string) {
        return NXRT_SEC_INVALID_INPUT;
    }
    
    return NXRT_SEC_OK;
}

/* ============ Secure Memory Implementation ============ */

/* Volatile pointer to prevent optimization of zeroing */
typedef void *(*memset_t)(void *, int, size_t);
static volatile memset_t secure_memset_ptr = memset;

void nxrt_secure_zero(void *ptr, size_t size) {
    if (ptr && size > 0) {
        secure_memset_ptr(ptr, 0, size);
        /* Memory barrier to ensure zeroing completes */
        __asm__ __volatile__("" : : "r"(ptr) : "memory");
    }
}

void* nxrt_secure_alloc(size_t size) {
    if (size == 0) return NULL;
    
    /* Allocate with extra space for size header */
    size_t total = size + sizeof(size_t);
    void *ptr = malloc(total);
    if (!ptr) return NULL;
    
    /* Store size at beginning */
    *(size_t*)ptr = size;
    
    /* Zero the user portion */
    void *user_ptr = (char*)ptr + sizeof(size_t);
    nxrt_secure_zero(user_ptr, size);
    
    return user_ptr;
}

void* nxrt_secure_realloc(void *ptr, size_t old_size, size_t new_size) {
    if (!ptr) return nxrt_secure_alloc(new_size);
    if (new_size == 0) {
        nxrt_secure_free(ptr, old_size);
        return NULL;
    }
    
    void *new_ptr = nxrt_secure_alloc(new_size);
    if (!new_ptr) return NULL;
    
    size_t copy_size = old_size < new_size ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);
    
    nxrt_secure_free(ptr, old_size);
    
    return new_ptr;
}

void nxrt_secure_free(void *ptr, size_t size) {
    if (!ptr) return;
    
    /* Zero before freeing */
    nxrt_secure_zero(ptr, size);
    
    /* Get actual allocation pointer */
    void *real_ptr = (char*)ptr - sizeof(size_t);
    free(real_ptr);
}

int nxrt_secure_compare(const void *a, const void *b, size_t size) {
    const volatile unsigned char *pa = a;
    const volatile unsigned char *pb = b;
    volatile unsigned char diff = 0;
    
    for (size_t i = 0; i < size; i++) {
        diff |= pa[i] ^ pb[i];
    }
    
    return diff;
}

/* ============ Sandbox Implementation ============ */

#define MAX_SANDBOX_PATHS 32
#define MAX_SANDBOX_CHANNELS 16

struct nxrt_sandbox {
    nxrt_app_t app_id;
    char root_dir[PATH_MAX];
    char allowed_paths[MAX_SANDBOX_PATHS][PATH_MAX];
    int path_count;
    char allowed_channels[MAX_SANDBOX_CHANNELS][64];
    int channel_count;
};

nxrt_sandbox_t* nxrt_sandbox_create(nxrt_app_t app_id, const char *root_dir) {
    if (!root_dir) return NULL;
    
    nxrt_sandbox_t *sb = calloc(1, sizeof(nxrt_sandbox_t));
    if (!sb) return NULL;
    
    sb->app_id = app_id;
    strncpy(sb->root_dir, root_dir, PATH_MAX - 1);
    
    /* Allow root dir by default */
    strncpy(sb->allowed_paths[0], root_dir, PATH_MAX - 1);
    sb->path_count = 1;
    
    /* Allow system IPC channel */
    strncpy(sb->allowed_channels[0], "nxrt.system", 63);
    sb->channel_count = 1;
    
    printf("[NXRT Security] Sandbox created for app %d: %s\n", app_id, root_dir);
    
    return sb;
}

void nxrt_sandbox_destroy(nxrt_sandbox_t *sandbox) {
    if (sandbox) {
        nxrt_secure_zero(sandbox, sizeof(nxrt_sandbox_t));
        free(sandbox);
    }
}

int nxrt_sandbox_check_path(nxrt_sandbox_t *sandbox, const char *path) {
    if (!sandbox || !path) return 0;
    
    /* Resolve path */
    char resolved[PATH_MAX];
    if (nxrt_validate_path(path, sandbox->root_dir, resolved, PATH_MAX) != NXRT_SEC_OK) {
        return 0;
    }
    
    /* Check against allowed paths */
    for (int i = 0; i < sandbox->path_count; i++) {
        if (strncmp(resolved, sandbox->allowed_paths[i], 
                    strlen(sandbox->allowed_paths[i])) == 0) {
            return 1;
        }
    }
    
    return 0;
}

int nxrt_sandbox_check_ipc(nxrt_sandbox_t *sandbox, const char *channel) {
    if (!sandbox || !channel) return 0;
    
    for (int i = 0; i < sandbox->channel_count; i++) {
        if (strcmp(channel, sandbox->allowed_channels[i]) == 0) {
            return 1;
        }
    }
    
    nxrt_security_log(1, sandbox->app_id, "IPC channel blocked by sandbox");
    return 0;
}

void nxrt_sandbox_allow_path(nxrt_sandbox_t *sandbox, const char *pattern) {
    if (!sandbox || !pattern || sandbox->path_count >= MAX_SANDBOX_PATHS) return;
    
    strncpy(sandbox->allowed_paths[sandbox->path_count++], pattern, PATH_MAX - 1);
}

void nxrt_sandbox_allow_ipc(nxrt_sandbox_t *sandbox, const char *channel) {
    if (!sandbox || !channel || sandbox->channel_count >= MAX_SANDBOX_CHANNELS) return;
    
    strncpy(sandbox->allowed_channels[sandbox->channel_count++], channel, 63);
}

/* ============ Permission Enforcement ============ */

nxrt_security_error_t nxrt_enforce_permission(
    nxrt_app_t app_id,
    nxrt_permission_t perm,
    const char *operation) {
    
    if (!nxrt_permission_check(app_id, perm)) {
        char msg[256];
        snprintf(msg, 255, "Permission denied: %s (perm=%d)", operation, perm);
        nxrt_security_log(2, app_id, msg);
        return NXRT_SEC_PERMISSION_DENIED;
    }
    
    return NXRT_SEC_OK;
}

void nxrt_security_log(int level, nxrt_app_t app_id, const char *event) {
    const char *level_str = level == 0 ? "INFO" : (level == 1 ? "WARN" : "ERROR");
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, 31, "%Y-%m-%d %H:%M:%S", tm_info);
    
    if (app_id >= 0) {
        fprintf(stderr, "[NXRT Security] [%s] [%s] App %d: %s\n", 
                time_buf, level_str, app_id, event);
    } else {
        fprintf(stderr, "[NXRT Security] [%s] [%s] %s\n", 
                time_buf, level_str, event);
    }
}

/* ============ Crypto Implementation ============ */

/* Simplified crypto - production would use OpenSSL/libsodium */

struct nxrt_crypto {
    uint8_t key[32];
    size_t key_size;
};

nxrt_crypto_t* nxrt_crypto_create(const uint8_t *key, size_t key_size) {
    if (!key || (key_size != 16 && key_size != 24 && key_size != 32)) {
        return NULL;
    }
    
    nxrt_crypto_t *ctx = nxrt_secure_alloc(sizeof(nxrt_crypto_t));
    if (!ctx) return NULL;
    
    memcpy(ctx->key, key, key_size);
    ctx->key_size = key_size;
    
    return ctx;
}

void nxrt_crypto_destroy(nxrt_crypto_t *ctx) {
    if (ctx) {
        nxrt_secure_free(ctx, sizeof(nxrt_crypto_t));
    }
}

/* Simple XOR-based encryption (placeholder for real AES-GCM) */
nxrt_security_error_t nxrt_crypto_encrypt(
    nxrt_crypto_t *ctx,
    const uint8_t *plaintext,
    size_t plaintext_size,
    uint8_t *ciphertext,
    size_t *ciphertext_size) {
    
    if (!ctx || !plaintext || !ciphertext || !ciphertext_size) {
        return NXRT_SEC_INVALID_INPUT;
    }
    
    /* Need space for: nonce (12) + ciphertext + tag (16) */
    size_t needed = 12 + plaintext_size + 16;
    if (*ciphertext_size < needed) {
        return NXRT_SEC_BUFFER_OVERFLOW;
    }
    
    /* Generate nonce */
    if (nxrt_crypto_random(ciphertext, 12) != NXRT_SEC_OK) {
        return NXRT_SEC_ENCRYPTION_FAILED;
    }
    
    /* Simple XOR encryption (replace with AES-GCM in production) */
    for (size_t i = 0; i < plaintext_size; i++) {
        ciphertext[12 + i] = plaintext[i] ^ ctx->key[i % ctx->key_size];
    }
    
    /* Simple tag (replace with GCM tag in production) */
    uint8_t tag[16] = {0};
    for (size_t i = 0; i < plaintext_size; i++) {
        tag[i % 16] ^= plaintext[i];
    }
    memcpy(ciphertext + 12 + plaintext_size, tag, 16);
    
    *ciphertext_size = needed;
    return NXRT_SEC_OK;
}

nxrt_security_error_t nxrt_crypto_decrypt(
    nxrt_crypto_t *ctx,
    const uint8_t *ciphertext,
    size_t ciphertext_size,
    uint8_t *plaintext,
    size_t *plaintext_size) {
    
    if (!ctx || !ciphertext || !plaintext || !plaintext_size) {
        return NXRT_SEC_INVALID_INPUT;
    }
    
    if (ciphertext_size < 28) {  /* 12 nonce + 16 tag minimum */
        return NXRT_SEC_INVALID_INPUT;
    }
    
    size_t data_size = ciphertext_size - 12 - 16;
    if (*plaintext_size < data_size) {
        return NXRT_SEC_BUFFER_OVERFLOW;
    }
    
    /* Decrypt (XOR with key) */
    for (size_t i = 0; i < data_size; i++) {
        plaintext[i] = ciphertext[12 + i] ^ ctx->key[i % ctx->key_size];
    }
    
    /* Verify tag */
    uint8_t computed_tag[16] = {0};
    for (size_t i = 0; i < data_size; i++) {
        computed_tag[i % 16] ^= plaintext[i];
    }
    
    if (nxrt_secure_compare(computed_tag, ciphertext + 12 + data_size, 16) != 0) {
        nxrt_secure_zero(plaintext, data_size);
        return NXRT_SEC_DECRYPTION_FAILED;
    }
    
    *plaintext_size = data_size;
    return NXRT_SEC_OK;
}

nxrt_security_error_t nxrt_crypto_random(uint8_t *buffer, size_t size) {
    if (!buffer) return NXRT_SEC_INVALID_INPUT;
    
#ifdef __linux__
    ssize_t result = getrandom(buffer, size, 0);
    if (result != (ssize_t)size) {
        return NXRT_SEC_ENCRYPTION_FAILED;
    }
    return NXRT_SEC_OK;
#else
    /* Fallback - not cryptographically secure */
    for (size_t i = 0; i < size; i++) {
        buffer[i] = rand() & 0xFF;
    }
    return NXRT_SEC_OK;
#endif
}

/* Simple hash (replace with SHA-256 in production) */
nxrt_security_error_t nxrt_crypto_hash(
    const uint8_t *data,
    size_t data_size,
    uint8_t hash[32]) {
    
    if (!data || !hash) return NXRT_SEC_INVALID_INPUT;
    
    /* Simple FNV-1a based hash (not SHA-256) */
    memset(hash, 0, 32);
    uint64_t h = 0xcbf29ce484222325ULL;
    
    for (size_t i = 0; i < data_size; i++) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
        hash[i % 32] ^= (h >> ((i % 8) * 8)) & 0xFF;
    }
    
    return NXRT_SEC_OK;
}

nxrt_security_error_t nxrt_crypto_derive_key(
    const char *password,
    const uint8_t *salt,
    size_t salt_size,
    int iterations,
    uint8_t *key,
    size_t key_size) {
    
    if (!password || !salt || !key) return NXRT_SEC_INVALID_INPUT;
    
    /* Simple key derivation (replace with PBKDF2 in production) */
    size_t pass_len = strlen(password);
    
    /* Combine password and salt */
    size_t combined_size = pass_len + salt_size;
    uint8_t *combined = malloc(combined_size);
    if (!combined) return NXRT_SEC_INVALID_INPUT;
    
    memcpy(combined, password, pass_len);
    memcpy(combined + pass_len, salt, salt_size);
    
    uint8_t hash[32];
    nxrt_crypto_hash(combined, combined_size, hash);
    
    /* Iterate */
    for (int i = 0; i < iterations; i++) {
        uint8_t temp[32];
        nxrt_crypto_hash(hash, 32, temp);
        memcpy(hash, temp, 32);
    }
    
    /* Copy to output */
    memcpy(key, hash, key_size < 32 ? key_size : 32);
    
    nxrt_secure_zero(combined, combined_size);
    free(combined);
    
    return NXRT_SEC_OK;
}
