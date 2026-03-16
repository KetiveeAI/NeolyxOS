/*
 * NeolyxOS Installer - Manifest Parser
 * Parses manifest.npa JSON files
 * 
 * Copyright (c) 2025-2026 KetiveeAI. All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/installer.h"

/* Simple JSON parser - extracts string values */
static bool json_get_string(const char* json, const char* key, char* out, size_t out_len) {
    if (!json || !key || !out) return false;
    
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char* pos = strstr(json, pattern);
    if (!pos) return false;
    
    /* Find the colon */
    pos = strchr(pos, ':');
    if (!pos) return false;
    pos++;
    
    /* Skip whitespace */
    while (*pos && isspace(*pos)) pos++;
    
    /* Expect opening quote */
    if (*pos != '"') return false;
    pos++;
    
    /* Copy until closing quote */
    size_t i = 0;
    while (*pos && *pos != '"' && i < out_len - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
            switch (*pos) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                default: out[i++] = *pos; break;
            }
        } else {
            out[i++] = *pos;
        }
        pos++;
    }
    out[i] = '\0';
    
    return i > 0;
}

/* Get integer value from JSON */
static bool json_get_int(const char* json, const char* key, int64_t* out) {
    if (!json || !key || !out) return false;
    
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char* pos = strstr(json, pattern);
    if (!pos) return false;
    
    pos = strchr(pos, ':');
    if (!pos) return false;
    pos++;
    
    while (*pos && isspace(*pos)) pos++;
    
    char* endptr;
    *out = strtoll(pos, &endptr, 10);
    
    return endptr != pos;
}

/* Get array of strings (permissions) */
static uint32_t json_get_permissions(const char* json) {
    uint32_t perms = 0;
    
    const char* pos = strstr(json, "\"permissions\"");
    if (!pos) return 0;
    
    pos = strchr(pos, '[');
    if (!pos) return 0;
    
    const char* end = strchr(pos, ']');
    if (!end) return 0;
    
    /* Check each permission string */
    if (strstr(pos, "\"filesystem.read\"") && strstr(pos, "\"filesystem.read\"") < end)
        perms |= PERM_FILESYSTEM_READ;
    if (strstr(pos, "\"filesystem.write\"") && strstr(pos, "\"filesystem.write\"") < end)
        perms |= PERM_FILESYSTEM_WRITE;
    if (strstr(pos, "\"network\"") && strstr(pos, "\"network\"") < end)
        perms |= PERM_NETWORK;
    if (strstr(pos, "\"camera\"") && strstr(pos, "\"camera\"") < end)
        perms |= PERM_CAMERA;
    if (strstr(pos, "\"microphone\"") && strstr(pos, "\"microphone\"") < end)
        perms |= PERM_MICROPHONE;
    if (strstr(pos, "\"location\"") && strstr(pos, "\"location\"") < end)
        perms |= PERM_LOCATION;
    if (strstr(pos, "\"bluetooth\"") && strstr(pos, "\"bluetooth\"") < end)
        perms |= PERM_BLUETOOTH;
    if (strstr(pos, "\"system.registry\"") && strstr(pos, "\"system.registry\"") < end)
        perms |= PERM_SYSTEM_REGISTRY;
    
    return perms;
}

/* Parse manifest JSON into structure */
bool manifest_parse(const char* json, app_manifest_t* manifest) {
    if (!json || !manifest) return false;
    
    memset(manifest, 0, sizeof(app_manifest_t));
    
    /* Required fields */
    if (!json_get_string(json, "name", manifest->name, sizeof(manifest->name))) {
        fprintf(stderr, "Error: Manifest missing 'name' field\n");
        return false;
    }
    
    if (!json_get_string(json, "bundle_id", manifest->bundle_id, sizeof(manifest->bundle_id))) {
        fprintf(stderr, "Error: Manifest missing 'bundle_id' field\n");
        return false;
    }
    
    /* Optional fields with defaults */
    if (!json_get_string(json, "version", manifest->version, sizeof(manifest->version))) {
        strcpy(manifest->version, "1.0.0");
    }
    
    if (!json_get_string(json, "category", manifest->category, sizeof(manifest->category))) {
        strcpy(manifest->category, "Other");
    }
    
    json_get_string(json, "description", manifest->description, sizeof(manifest->description));
    json_get_string(json, "author", manifest->author, sizeof(manifest->author));
    json_get_string(json, "binary", manifest->binary, sizeof(manifest->binary));
    json_get_string(json, "icon", manifest->icon, sizeof(manifest->icon));
    
    /* Size */
    int64_t size = 0;
    if (json_get_int(json, "size", &size)) {
        manifest->size_bytes = (uint64_t)size;
    }
    
    /* Permissions */
    manifest->permissions = json_get_permissions(json);
    
    return true;
}

/* Convert permission flag to human-readable string */
const char* permission_to_string(uint32_t perm) {
    switch (perm) {
        case PERM_FILESYSTEM_READ:  return "Read files from disk";
        case PERM_FILESYSTEM_WRITE: return "Write files to disk";
        case PERM_NETWORK:          return "Access the network";
        case PERM_CAMERA:           return "Use the camera";
        case PERM_MICROPHONE:       return "Use the microphone";
        case PERM_LOCATION:         return "Access your location";
        case PERM_BLUETOOTH:        return "Use Bluetooth";
        case PERM_SYSTEM_REGISTRY:  return "Modify system registry";
        default:                    return "Unknown permission";
    }
}
