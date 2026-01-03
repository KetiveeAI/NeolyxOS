/*
 * NeolyxOS Native API Bindings for ZebraScript
 * 
 * Exposes NeolyxOS system APIs to JavaScript applications.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef NXAPI_H
#define NXAPI_H

#include "../../include/zeprascript.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * System APIs
 * ============================================================================ */

/* Initialize all NX API bindings */
void nxapi_init(zs_context *ctx);

/* Individual API modules */
void nxapi_init_system(zs_context *ctx);     /* System info, power, etc. */
void nxapi_init_fs(zs_context *ctx);         /* File system operations */
void nxapi_init_process(zs_context *ctx);    /* Process management */
void nxapi_init_ui(zs_context *ctx);         /* UI/window management */
void nxapi_init_network(zs_context *ctx);    /* Network operations */
void nxapi_init_storage(zs_context *ctx);    /* Local storage */
void nxapi_init_crypto(zs_context *ctx);     /* Cryptography */
void nxapi_init_media(zs_context *ctx);      /* Audio/video */

/* ============================================================================
 * NX.System APIs
 * Available as: NX.System.getInfo(), NX.System.shutdown(), etc.
 * ============================================================================ */

/* System information */
typedef struct nx_system_info {
    const char *os_name;
    const char *os_version;
    const char *kernel_version;
    const char *hostname;
    const char *architecture;
    int cpu_count;
    uint64_t memory_total;
    uint64_t memory_free;
    uint64_t uptime_seconds;
} nx_system_info;

nx_system_info *nxapi_system_get_info(void);

/* Power management */
void nxapi_system_shutdown(void);
void nxapi_system_reboot(void);
void nxapi_system_sleep(void);

/* ============================================================================
 * NX.FS (File System) APIs
 * Available as: NX.FS.readFile(), NX.FS.writeFile(), etc.
 * ============================================================================ */

/* Read file contents */
char *nxapi_fs_read_file(const char *path);

/* Write file contents */
bool nxapi_fs_write_file(const char *path, const char *content);

/* Check if file exists */
bool nxapi_fs_exists(const char *path);

/* Create directory */
bool nxapi_fs_mkdir(const char *path);

/* List directory contents */
char **nxapi_fs_readdir(const char *path, int *count);

/* Delete file or directory */
bool nxapi_fs_remove(const char *path);

/* File stats */
typedef struct nx_file_stat {
    uint64_t size;
    uint64_t created;
    uint64_t modified;
    bool is_directory;
    bool is_file;
    bool is_symlink;
} nx_file_stat;

nx_file_stat *nxapi_fs_stat(const char *path);

/* ============================================================================
 * NX.Process APIs
 * Available as: NX.Process.spawn(), NX.Process.exit(), etc.
 * ============================================================================ */

/* Spawn new process */
int nxapi_process_spawn(const char *path, char **args);

/* Exit current process */
void nxapi_process_exit(int code);

/* Get process ID */
int nxapi_process_getpid(void);

/* Get environment variable */
const char *nxapi_process_getenv(const char *name);

/* Set environment variable */
void nxapi_process_setenv(const char *name, const char *value);

/* ============================================================================
 * NX.UI APIs
 * Available as: NX.UI.alert(), NX.UI.confirm(), etc.
 * ============================================================================ */

/* Show alert dialog */
void nxapi_ui_alert(const char *message);

/* Show confirm dialog */
bool nxapi_ui_confirm(const char *message);

/* Show notification */
void nxapi_ui_notify(const char *title, const char *message);

/* Show file picker */
char *nxapi_ui_file_picker(bool save_mode, const char *filter);

/* ============================================================================
 * NX.Storage (Local Storage) APIs
 * Available as: NX.Storage.get(), NX.Storage.set(), etc.
 * ============================================================================ */

/* Get stored value */
char *nxapi_storage_get(const char *key);

/* Set stored value */
void nxapi_storage_set(const char *key, const char *value);

/* Remove stored value */
void nxapi_storage_remove(const char *key);

/* Clear all storage */
void nxapi_storage_clear(void);

/* ============================================================================
 * NX.Network APIs
 * Available as: NX.Network.fetch(), NX.Network.getStatus(), etc.
 * ============================================================================ */

/* Network status */
typedef struct nx_network_status {
    bool connected;
    bool wifi_enabled;
    const char *ip_address;
    const char *ssid;
    int signal_strength;
} nx_network_status;

nx_network_status *nxapi_network_get_status(void);

/* Simple HTTP fetch (async in JS) */
typedef struct nx_http_response {
    int status;
    char *body;
    char **headers;
    int header_count;
} nx_http_response;

nx_http_response *nxapi_network_fetch(const char *url, const char *method, 
                                       const char *body);

/* ============================================================================
 * NX.Crypto APIs
 * Available as: NX.Crypto.hash(), NX.Crypto.encrypt(), etc.
 * ============================================================================ */

/* Hash string */
char *nxapi_crypto_hash(const char *data, const char *algorithm);

/* Generate random bytes */
void nxapi_crypto_random_bytes(uint8_t *buffer, size_t length);

/* Generate UUID */
char *nxapi_crypto_uuid(void);

/* ============================================================================
 * Global Objects Exposed
 * ============================================================================ */

/*
 * After nxapi_init(ctx), these objects are available in JavaScript:
 *
 * NX.System
 *   .getInfo() -> { osName, osVersion, hostname, cpuCount, memoryTotal, ... }
 *   .shutdown()
 *   .reboot()
 *   .sleep()
 *
 * NX.FS
 *   .readFile(path) -> string
 *   .writeFile(path, content) -> boolean
 *   .exists(path) -> boolean
 *   .mkdir(path) -> boolean
 *   .readdir(path) -> string[]
 *   .stat(path) -> { size, created, modified, isDirectory, ... }
 *
 * NX.Process
 *   .spawn(path, args) -> number (pid)
 *   .exit(code)
 *   .getpid() -> number
 *   .env.get(name) -> string
 *   .env.set(name, value)
 *
 * NX.UI
 *   .alert(message)
 *   .confirm(message) -> boolean
 *   .notify(title, message)
 *   .filePicker(saveMode, filter) -> string
 *
 * NX.Storage
 *   .get(key) -> string
 *   .set(key, value)
 *   .remove(key)
 *   .clear()
 *
 * NX.Network
 *   .fetch(url, options) -> Promise<Response>
 *   .getStatus() -> { connected, wifiEnabled, ipAddress, ... }
 *
 * NX.Crypto
 *   .hash(data, algorithm) -> string
 *   .randomBytes(length) -> Uint8Array
 *   .uuid() -> string
 */

#ifdef __cplusplus
}
#endif

#endif /* NXAPI_H */
