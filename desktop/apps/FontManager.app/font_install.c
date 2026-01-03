/*
 * NeolyxOS Font Manager - Font Installation Module
 * 
 * Handles font import, installation, and removal.
 * Supports TTF, OTF, WOFF, and NXF (NeolyxOS Font) formats.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: LicenseRef-KetiveeAI-Proprietary
 */

#include "font_service.h"
#include "state.h"
#include <stdint.h>
#include <stdbool.h>

/* ============ Font Format Detection ============ */

typedef enum {
    FONT_FORMAT_UNKNOWN = 0,
    FONT_FORMAT_TTF,        /* TrueType */
    FONT_FORMAT_OTF,        /* OpenType */
    FONT_FORMAT_WOFF,       /* Web Open Font Format */
    FONT_FORMAT_WOFF2,      /* WOFF2 */
    FONT_FORMAT_NXF         /* NeolyxOS Native Font */
} font_format_t;

/* Magic bytes for format detection */
#define TTF_MAGIC       0x00010000  /* TrueType */
#define OTF_MAGIC       0x4F54544F  /* 'OTTO' - OpenType CFF */
#define WOFF_MAGIC      0x774F4646  /* 'wOFF' */
#define WOFF2_MAGIC     0x774F4632  /* 'wOF2' */
#define NXF_MAGIC       0x4E584600  /* 'NXF\0' */

/* Detect font format from file header */
static font_format_t detect_font_format(const uint8_t *data, uint32_t size) {
    if (!data || size < 4) return FONT_FORMAT_UNKNOWN;
    
    uint32_t magic = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    
    switch (magic) {
        case TTF_MAGIC:
            return FONT_FORMAT_TTF;
        case OTF_MAGIC:
            return FONT_FORMAT_OTF;
        case WOFF_MAGIC:
            return FONT_FORMAT_WOFF;
        case WOFF2_MAGIC:
            return FONT_FORMAT_WOFF2;
    }
    
    /* Check for NXF (our format) */
    if (data[0] == 'N' && data[1] == 'X' && data[2] == 'F') {
        return FONT_FORMAT_NXF;
    }
    
    /* TTF can also start with 'true' */
    if (data[0] == 't' && data[1] == 'r' && data[2] == 'u' && data[3] == 'e') {
        return FONT_FORMAT_TTF;
    }
    
    return FONT_FORMAT_UNKNOWN;
}

/* Get format name string */
static const char* font_format_name(font_format_t format) {
    switch (format) {
        case FONT_FORMAT_TTF:   return "TrueType";
        case FONT_FORMAT_OTF:   return "OpenType";
        case FONT_FORMAT_WOFF:  return "WOFF";
        case FONT_FORMAT_WOFF2: return "WOFF2";
        case FONT_FORMAT_NXF:   return "NeolyxOS Font";
        default:                return "Unknown";
    }
}

/* ============ Font Validation ============ */

/* Validate font file structure */
int font_validate(const uint8_t *data, uint32_t size, char *error_msg, int error_size) {
    if (!data || size < 12) {
        if (error_msg) snprintf(error_msg, error_size, "File too small");
        return NX_FONT_INVALID;
    }
    
    font_format_t format = detect_font_format(data, size);
    
    if (format == FONT_FORMAT_UNKNOWN) {
        if (error_msg) snprintf(error_msg, error_size, "Unknown font format");
        return NX_FONT_INVALID;
    }
    
    /* Basic structure validation by format */
    switch (format) {
        case FONT_FORMAT_TTF:
        case FONT_FORMAT_OTF: {
            /* Check table count */
            uint16_t table_count = (data[4] << 8) | data[5];
            if (table_count == 0 || table_count > 100) {
                if (error_msg) snprintf(error_msg, error_size, "Invalid table count");
                return NX_FONT_CORRUPTED;
            }
            
            /* Minimum expected size for tables */
            uint32_t min_size = 12 + table_count * 16;
            if (size < min_size) {
                if (error_msg) snprintf(error_msg, error_size, "File truncated");
                return NX_FONT_CORRUPTED;
            }
            break;
        }
        
        case FONT_FORMAT_NXF: {
            /* Check NXF version */
            if (data[3] > 2) {
                if (error_msg) snprintf(error_msg, error_size, "Unsupported NXF version");
                return NX_FONT_INVALID;
            }
            break;
        }
        
        default:
            break;
    }
    
    return NX_FONT_OK;
}

/* ============ Font Installation ============ */

/* Font installation context */
typedef struct {
    char source_path[256];
    char dest_path[256];
    char family_name[64];
    char style_name[32];
    font_format_t format;
    bool as_system;
    bool overwrite;
} font_install_ctx_t;

/* Parse font metadata from TTF/OTF name table */
static int parse_font_name(const uint8_t *data, uint32_t size, 
                           char *family, int family_size,
                           char *style, int style_size) {
    if (!data || size < 12) return -1;
    
    /* Find 'name' table */
    uint16_t table_count = (data[4] << 8) | data[5];
    uint32_t table_offset = 12;
    
    for (int i = 0; i < table_count; i++) {
        uint32_t tag = (data[table_offset] << 24) | (data[table_offset+1] << 16) |
                       (data[table_offset+2] << 8) | data[table_offset+3];
        
        if (tag == 0x6E616D65) {  /* 'name' */
            uint32_t name_offset = (data[table_offset+8] << 24) | (data[table_offset+9] << 16) |
                                   (data[table_offset+10] << 8) | data[table_offset+11];
            
            /* Parse name table */
            if (name_offset + 6 > size) return -1;
            
            uint16_t count = (data[name_offset+2] << 8) | data[name_offset+3];
            uint16_t string_offset = (data[name_offset+4] << 8) | data[name_offset+5];
            uint32_t string_base = name_offset + string_offset;
            
            /* Look for family name (nameID 1) and style (nameID 2) */
            for (int j = 0; j < count && j < 100; j++) {
                uint32_t record_offset = name_offset + 6 + j * 12;
                if (record_offset + 12 > size) break;
                
                uint16_t platform = (data[record_offset] << 8) | data[record_offset+1];
                uint16_t name_id = (data[record_offset+6] << 8) | data[record_offset+7];
                uint16_t length = (data[record_offset+8] << 8) | data[record_offset+9];
                uint16_t offset = (data[record_offset+10] << 8) | data[record_offset+11];
                
                /* Prefer platform 3 (Windows) or 1 (Mac) */
                if (platform != 3 && platform != 1) continue;
                if (string_base + offset + length > size) continue;
                
                if (name_id == 1 && family) {
                    /* Family name */
                    int len = (length < family_size - 1) ? length : family_size - 1;
                    for (int k = 0; k < len; k++) {
                        uint8_t c = data[string_base + offset + k];
                        if (c >= 32 && c < 127) family[k/2] = c;  /* Skip high bytes for Unicode */
                    }
                    family[len/2] = '\0';
                }
                else if (name_id == 2 && style) {
                    /* Style name */
                    int len = (length < style_size - 1) ? length : style_size - 1;
                    for (int k = 0; k < len; k++) {
                        uint8_t c = data[string_base + offset + k];
                        if (c >= 32 && c < 127) style[k/2] = c;
                    }
                    style[len/2] = '\0';
                }
            }
            return 0;
        }
        table_offset += 16;
    }
    
    return -1;
}

/* Install font from file data */
int font_install_from_data(const uint8_t *data, uint32_t size, 
                           const char *filename, bool as_system) {
    char error_msg[128];
    
    /* Validate font */
    int result = font_validate(data, size, error_msg, sizeof(error_msg));
    if (result != NX_FONT_OK) {
        return result;
    }
    
    font_format_t format = detect_font_format(data, size);
    
    /* Parse font name */
    char family[64] = "Unknown";
    char style[32] = "Regular";
    parse_font_name(data, size, family, sizeof(family), style, sizeof(style));
    
    /* Build destination path */
    const char *base_path = as_system ? "/System/Library/Fonts" : "/Library/Fonts";
    char dest_path[256];
    const char *ext = (format == FONT_FORMAT_OTF) ? ".otf" : 
                      (format == FONT_FORMAT_NXF) ? ".nxf" : ".ttf";
    snprintf(dest_path, sizeof(dest_path), "%s/%s-%s%s", base_path, family, style, ext);
    
    /* Check if font already exists */
    /* TODO: VFS file exists check */
    
    /* Copy font to destination */
    /* TODO: VFS file write */
    
    /* Register font in system */
    /* TODO: Update font registry */
    
    return NX_FONT_OK;
}

/* ============ Drag and Drop Support ============ */

/* Check if dropped file is a valid font */
bool font_can_import(const char *path) {
    if (!path) return false;
    
    /* Check extension */
    int len = 0;
    while (path[len]) len++;
    
    if (len < 4) return false;
    
    const char *ext = &path[len - 4];
    
    if (ext[0] == '.') {
        /* Check common font extensions */
        if ((ext[1] == 't' || ext[1] == 'T') &&
            (ext[2] == 't' || ext[2] == 'T') &&
            (ext[3] == 'f' || ext[3] == 'F')) {
            return true;  /* .ttf */
        }
        if ((ext[1] == 'o' || ext[1] == 'O') &&
            (ext[2] == 't' || ext[2] == 'T') &&
            (ext[3] == 'f' || ext[3] == 'F')) {
            return true;  /* .otf */
        }
        if ((ext[1] == 'n' || ext[1] == 'N') &&
            (ext[2] == 'x' || ext[2] == 'X') &&
            (ext[3] == 'f' || ext[3] == 'F')) {
            return true;  /* .nxf */
        }
    }
    
    /* Check .woff extension (5 chars) */
    if (len >= 5) {
        ext = &path[len - 5];
        if (ext[0] == '.' &&
            (ext[1] == 'w' || ext[1] == 'W') &&
            (ext[2] == 'o' || ext[2] == 'O') &&
            (ext[3] == 'f' || ext[3] == 'F') &&
            (ext[4] == 'f' || ext[4] == 'F')) {
            return true;  /* .woff */
        }
    }
    
    return false;
}

/* Handle dropped font file */
int font_import_file(const char *path) {
    if (!font_can_import(path)) {
        return NX_FONT_INVALID;
    }
    
    /* TODO: Read file from VFS */
    /* TODO: Install font using font_install_from_data() */
    
    return NX_FONT_OK;
}

/* ============ Font Removal ============ */

/* Remove user-installed font */
int font_remove(uint32_t font_id) {
    /* Cannot remove system fonts */
    /* TODO: Check if font is system font */
    
    /* TODO: Delete font file from /Library/Fonts */
    /* TODO: Update font registry */
    
    return NX_FONT_OK;
}

/* ============ Font Family Grouping ============ */

/* Group fonts by family */
typedef struct {
    char family[64];
    uint32_t font_ids[16];  /* Up to 16 styles per family */
    int font_count;
} font_family_group_t;

#define MAX_FONT_FAMILIES 64

static font_family_group_t g_font_families[MAX_FONT_FAMILIES];
static int g_family_count = 0;

/* Rebuild family groupings */
void font_rebuild_families(fm_state_t *state) {
    g_family_count = 0;
    
    for (uint32_t i = 0; i < state->font_count; i++) {
        const fm_font_entry_t *f = &state->fonts[i];
        
        /* Find existing family */
        int family_idx = -1;
        for (int j = 0; j < g_family_count; j++) {
            int match = 1;
            for (int k = 0; g_font_families[j].family[k] && f->family[k]; k++) {
                if (g_font_families[j].family[k] != f->family[k]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                family_idx = j;
                break;
            }
        }
        
        /* Create new family if not found */
        if (family_idx < 0 && g_family_count < MAX_FONT_FAMILIES) {
            family_idx = g_family_count++;
            int k = 0;
            while (f->family[k] && k < 63) {
                g_font_families[family_idx].family[k] = f->family[k];
                k++;
            }
            g_font_families[family_idx].family[k] = '\0';
            g_font_families[family_idx].font_count = 0;
        }
        
        /* Add font to family */
        if (family_idx >= 0) {
            font_family_group_t *fam = &g_font_families[family_idx];
            if (fam->font_count < 16) {
                fam->font_ids[fam->font_count++] = f->id;
            }
        }
    }
}

/* Get family groups */
const font_family_group_t* font_get_families(int *count) {
    if (count) *count = g_family_count;
    return g_font_families;
}
