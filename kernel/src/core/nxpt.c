/**
 * @file nxpt.c
 * @brief NeolyxOS Program Tree (.nxpt) Parser Implementation
 * 
 * Simple JSON-like parser for .nxpt configuration files.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <core/nxpt.h>
#include <string.h>

/* Local snprintf implementation for kernel */
static int local_snprintf(char *buf, size_t size, const char *fmt, const char *arg) {
    /* Simple format: "\"%s\"" only */
    size_t i = 0;
    while (*fmt && i < size - 1) {
        if (*fmt == '%' && *(fmt + 1) == 's') {
            while (*arg && i < size - 1) buf[i++] = *arg++;
            fmt += 2;
        } else {
            buf[i++] = *fmt++;
        }
    }
    buf[i] = '\0';
    return (int)i;
}

/* Simple JSON key-value extraction (no external deps) */
static const char* find_key(const char* json, const char* key) {
    char search[128];
    local_snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return NULL;
    pos = strchr(pos, ':');
    if (!pos) return NULL;
    pos++; /* Skip ':' */
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;
    return pos;
}

static int extract_string(const char* json, const char* key, char* out, size_t max) {
    const char* pos = find_key(json, key);
    if (!pos) return -1;
    if (*pos != '"') return -1;
    pos++; /* Skip opening quote */
    
    size_t i = 0;
    while (*pos && *pos != '"' && i < max - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return 0;
}

static int extract_bool(const char* json, const char* key, bool* out) {
    const char* pos = find_key(json, key);
    if (!pos) return -1;
    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return 0;
    } else if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

static int extract_int(const char* json, const char* key, uint32_t* out) {
    const char* pos = find_key(json, key);
    if (!pos) return -1;
    *out = 0;
    while (*pos >= '0' && *pos <= '9') {
        *out = (*out * 10) + (*pos - '0');
        pos++;
    }
    return 0;
}

/* Parse capability string to flag */
nxpt_capability_t nxpt_parse_capability(const char* name) {
    if (strcmp(name, "network.client") == 0) return NXCAP_NETWORK_CLIENT;
    if (strcmp(name, "network.server") == 0) return NXCAP_NETWORK_SERVER;
    if (strcmp(name, "filesystem.user") == 0) return NXCAP_FILESYSTEM_USER;
    if (strcmp(name, "filesystem.full") == 0) return NXCAP_FILESYSTEM_FULL;
    if (strcmp(name, "filesystem.downloads") == 0) return NXCAP_FILESYSTEM_DL;
    if (strcmp(name, "gpu.render") == 0) return NXCAP_GPU_RENDER;
    if (strcmp(name, "audio.playback") == 0) return NXCAP_AUDIO_PLAYBACK;
    if (strcmp(name, "audio.record") == 0) return NXCAP_AUDIO_RECORD;
    if (strcmp(name, "camera") == 0) return NXCAP_CAMERA;
    if (strcmp(name, "notifications") == 0) return NXCAP_NOTIFICATIONS;
    if (strcmp(name, "background.fetch") == 0) return NXCAP_BACKGROUND;
    if (strcmp(name, "zeprascript.engine") == 0) return NXCAP_ZEPRASCRIPT;
    if (strcmp(name, "nxgame.session") == 0) return NXCAP_NXGAME;
    if (strcmp(name, "kernel.driver") == 0) return NXCAP_KERNEL_DRIVER;
    return NXCAP_NONE;
}

const char* nxpt_capability_name(nxpt_capability_t cap) {
    switch (cap) {
        case NXCAP_NETWORK_CLIENT:  return "network.client";
        case NXCAP_NETWORK_SERVER:  return "network.server";
        case NXCAP_FILESYSTEM_USER: return "filesystem.user";
        case NXCAP_FILESYSTEM_FULL: return "filesystem.full";
        case NXCAP_FILESYSTEM_DL:   return "filesystem.downloads";
        case NXCAP_GPU_RENDER:      return "gpu.render";
        case NXCAP_AUDIO_PLAYBACK:  return "audio.playback";
        case NXCAP_AUDIO_RECORD:    return "audio.record";
        case NXCAP_CAMERA:          return "camera";
        case NXCAP_NOTIFICATIONS:   return "notifications";
        case NXCAP_BACKGROUND:      return "background.fetch";
        case NXCAP_ZEPRASCRIPT:     return "zeprascript.engine";
        case NXCAP_NXGAME:          return "nxgame.session";
        case NXCAP_KERNEL_DRIVER:   return "kernel.driver";
        default:                    return "unknown";
    }
}

/* Parse capabilities array from JSON */
static void parse_capabilities(const char* json, nxpt_info_t* info) {
    const char* pos = find_key(json, "NXCapabilities");
    if (!pos || *pos != '[') return;
    pos++; /* Skip '[' */
    
    char cap_buf[64];
    while (*pos) {
        while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == ',') pos++;
        if (*pos == ']') break;
        if (*pos != '"') break;
        
        pos++; /* Skip opening quote */
        size_t i = 0;
        while (*pos && *pos != '"' && i < sizeof(cap_buf) - 1) {
            cap_buf[i++] = *pos++;
        }
        cap_buf[i] = '\0';
        if (*pos == '"') pos++;
        
        nxpt_capability_t cap = nxpt_parse_capability(cap_buf);
        info->capabilities |= cap;
    }
}

/* Parse URL schemes array */
static void parse_url_schemes(const char* json, nxpt_info_t* info) {
    const char* pos = find_key(json, "NXURLSchemes");
    if (!pos || *pos != '[') return;
    pos++;
    
    info->url_scheme_count = 0;
    while (*pos && info->url_scheme_count < NXPT_MAX_URL_SCHEMES) {
        while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == ',') pos++;
        if (*pos == ']') break;
        if (*pos != '"') break;
        
        pos++;
        size_t i = 0;
        while (*pos && *pos != '"' && i < sizeof(info->url_schemes[0].scheme) - 1) {
            info->url_schemes[info->url_scheme_count].scheme[i++] = *pos++;
        }
        info->url_schemes[info->url_scheme_count].scheme[i] = '\0';
        info->url_schemes[info->url_scheme_count].is_default = false;
        if (*pos == '"') pos++;
        info->url_scheme_count++;
    }
}

int nxpt_parse(const char* data, size_t size, nxpt_info_t* info) {
    if (!data || !info || size == 0) return -1;
    
    memset(info, 0, sizeof(*info));
    
    /* Extract basic fields */
    extract_string(data, "NXAppName", info->app_name, sizeof(info->app_name));
    extract_string(data, "NXAppId", info->app_id, sizeof(info->app_id));
    extract_string(data, "NXAppVersion", info->version, sizeof(info->version));
    extract_int(data, "NXBuildNumber", &info->build_number);
    extract_string(data, "NXExecutable", info->executable, sizeof(info->executable));
    extract_string(data, "NXIcon", info->icon_path, sizeof(info->icon_path));
    extract_string(data, "NXCategory", info->category, sizeof(info->category));
    
    /* App type */
    extract_bool(data, "NXSystemApp", &info->is_system_app);
    extract_bool(data, "NXDefaultBrowser", &info->is_default_browser);
    extract_bool(data, "NXWebApp", &info->is_web_app);
    extract_string(data, "NXWebAppURL", info->web_app_url, sizeof(info->web_app_url));
    
    /* Parse capabilities */
    parse_capabilities(data, info);
    
    /* Parse URL schemes */
    parse_url_schemes(data, info);
    
    /* Metadata */
    extract_string(data, "NXMinOS", info->min_os, sizeof(info->min_os));
    extract_string(data, "NXCopyright", info->copyright, sizeof(info->copyright));
    extract_string(data, "NXWebsite", info->website, sizeof(info->website));
    
    return 0;
}

int nxpt_load(const char* path, nxpt_info_t* info) {
    /* In real kernel, use vfs_open/vfs_read */
    /* For now, this is a stub that would be implemented with VFS */
    (void)path;
    (void)info;
    return -1; /* Not implemented - use nxpt_parse with data */
}

void nxpt_free(nxpt_info_t* info) {
    /* No-op since we use fixed-size buffers */
    (void)info;
}
