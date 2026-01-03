/**
 * @file nxpt.h
 * @brief NeolyxOS Program Tree (.nxpt) Parser
 * 
 * .nxpt is the native configuration file format for NeolyxOS applications.
 * It uses JSON-like syntax and stores app metadata, capabilities, and settings.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef KERNEL_CORE_NXPT_H
#define KERNEL_CORE_NXPT_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum sizes */
#define NXPT_MAX_NAME         64
#define NXPT_MAX_ID           128
#define NXPT_MAX_PATH         256
#define NXPT_MAX_CAPABILITIES 32
#define NXPT_MAX_URL_SCHEMES  16
#define NXPT_MAX_DOC_TYPES    16

/* ------------------------------------------------------------------------- */
/* Capability Flags                                                          */
/* ------------------------------------------------------------------------- */

typedef enum {
    NXCAP_NONE             = 0,
    NXCAP_NETWORK_CLIENT   = (1 << 0),   /* Make outbound connections */
    NXCAP_NETWORK_SERVER   = (1 << 1),   /* Listen on ports */
    NXCAP_FILESYSTEM_USER  = (1 << 2),   /* Access user files */
    NXCAP_FILESYSTEM_FULL  = (1 << 3),   /* Full filesystem (admin) */
    NXCAP_FILESYSTEM_DL    = (1 << 4),   /* Downloads folder */
    NXCAP_GPU_RENDER       = (1 << 5),   /* GPU rendering */
    NXCAP_AUDIO_PLAYBACK   = (1 << 6),   /* Play audio */
    NXCAP_AUDIO_RECORD     = (1 << 7),   /* Record audio */
    NXCAP_CAMERA           = (1 << 8),   /* Camera access */
    NXCAP_NOTIFICATIONS    = (1 << 9),   /* System notifications */
    NXCAP_BACKGROUND       = (1 << 10),  /* Run in background */
    NXCAP_ZEPRASCRIPT      = (1 << 11),  /* ZepraScript engine */
    NXCAP_NXGAME           = (1 << 12),  /* NXGame bridge access */
    NXCAP_KERNEL_DRIVER    = (1 << 13),  /* Kernel driver (privileged) */
} nxpt_capability_t;

/* ------------------------------------------------------------------------- */
/* Document Type                                                             */
/* ------------------------------------------------------------------------- */

typedef struct {
    char extension[16];      /* File extension (html, pdf, etc.) */
    char mime_type[64];      /* MIME type (text/html, etc.) */
    char icon_path[128];     /* Path to icon for this type */
} nxpt_doctype_t;

/* ------------------------------------------------------------------------- */
/* URL Scheme                                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    char scheme[32];         /* URL scheme (http, zepra, lyx, etc.) */
    bool is_default;         /* Is this app the default handler? */
} nxpt_urlscheme_t;

/* ------------------------------------------------------------------------- */
/* Engine Info (for browsers/runtimes)                                       */
/* ------------------------------------------------------------------------- */

typedef struct {
    char name[NXPT_MAX_NAME];
    char version[16];
    uint32_t features;       /* Bitfield of engine features */
} nxpt_engine_t;

/* Engine feature flags */
#define NXENGINE_ES2024    (1 << 0)
#define NXENGINE_WEBAPI    (1 << 1)
#define NXENGINE_DOM       (1 << 2)
#define NXENGINE_FETCH     (1 << 3)
#define NXENGINE_WORKERS   (1 << 4)

/* ------------------------------------------------------------------------- */
/* Program Tree Structure                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
    /* Basic info */
    char app_name[NXPT_MAX_NAME];
    char app_id[NXPT_MAX_ID];       /* com.developer.appname */
    char version[16];
    uint32_t build_number;
    char executable[NXPT_MAX_NAME];
    char icon_path[NXPT_MAX_PATH];
    char category[32];
    
    /* App type */
    bool is_system_app;
    bool is_default_browser;
    bool is_web_app;
    char web_app_url[NXPT_MAX_PATH]; /* For web apps */
    
    /* Capabilities (bitfield) */
    uint32_t capabilities;
    
    /* URL schemes this app handles */
    nxpt_urlscheme_t url_schemes[NXPT_MAX_URL_SCHEMES];
    int url_scheme_count;
    
    /* Document types this app opens */
    nxpt_doctype_t doc_types[NXPT_MAX_DOC_TYPES];
    int doc_type_count;
    
    /* Engine info (if applicable) */
    nxpt_engine_t engine;
    bool has_engine;
    
    /* Requirements */
    char min_os[16];
    
    /* Metadata */
    char copyright[128];
    char website[NXPT_MAX_PATH];
} nxpt_info_t;

/* ------------------------------------------------------------------------- */
/* Parser API                                                                */
/* ------------------------------------------------------------------------- */

/**
 * Parse an .nxpt file from memory buffer
 * @param data     File contents (JSON format)
 * @param size     Size of data
 * @param info     Output structure to populate
 * @return         0 on success, negative on error
 */
int nxpt_parse(const char* data, size_t size, nxpt_info_t* info);

/**
 * Parse an .nxpt file from disk
 * @param path     Path to .nxpt file
 * @param info     Output structure to populate
 * @return         0 on success, negative on error
 */
int nxpt_load(const char* path, nxpt_info_t* info);

/**
 * Check if app has a specific capability
 * @param info     Parsed app info
 * @param cap      Capability to check
 * @return         true if app has capability
 */
static inline bool nxpt_has_capability(const nxpt_info_t* info, nxpt_capability_t cap) {
    return (info->capabilities & cap) != 0;
}

/**
 * Get capability name string
 * @param cap      Capability flag
 * @return         Human-readable name
 */
const char* nxpt_capability_name(nxpt_capability_t cap);

/**
 * Parse capability string to flag
 * @param name     Capability string (e.g., "network.client")
 * @return         Capability flag, or NXCAP_NONE if unknown
 */
nxpt_capability_t nxpt_parse_capability(const char* name);

/**
 * Free resources allocated by nxpt_parse/nxpt_load
 * (Currently a no-op since we use fixed-size buffers)
 */
void nxpt_free(nxpt_info_t* info);

#endif /* KERNEL_CORE_NXPT_H */
