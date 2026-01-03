/*
 * NeolyxOS Color Management System
 * Global color profiles and display calibration
 * Similar to Apple ColorSync
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef NXCOLOR_H
#define NXCOLOR_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Color Spaces ============ */

typedef enum {
    COLORSPACE_SRGB,         /* Standard RGB (default) */
    COLORSPACE_DISPLAY_P3,   /* Wide gamut (Apple) */
    COLORSPACE_ADOBE_RGB,    /* Adobe RGB (1998) */
    COLORSPACE_PROPHOTO_RGB, /* ProPhoto RGB */
    COLORSPACE_REC709,       /* HDTV standard */
    COLORSPACE_REC2020,      /* Ultra HD standard */
    COLORSPACE_DCI_P3,       /* Cinema standard */
    COLORSPACE_CMYK,         /* Print (4-channel) */
    COLORSPACE_LAB,          /* CIE L*a*b* */
    COLORSPACE_XYZ,          /* CIE XYZ */
    COLORSPACE_CUSTOM        /* User ICC profile */
} nxcolor_space_t;

/* ============ Color Profile ============ */

typedef struct {
    char name[64];
    char path[256];
    nxcolor_space_t colorspace;
    
    /* Primaries (xy chromaticity) */
    float red_x, red_y;
    float green_x, green_y;
    float blue_x, blue_y;
    float white_x, white_y;
    
    /* Gamma */
    float gamma;
    bool use_srgb_trc;  /* sRGB transfer curve */
    
    /* White point temperature */
    int white_point_k;  /* Kelvin (6500 = D65) */
    
    /* Metadata */
    char description[256];
    char manufacturer[64];
    char model[64];
    uint32_t version;
} nxcolor_profile_t;

/* ============ Display Settings ============ */

typedef struct {
    char display_name[64];
    int display_id;
    
    /* Current profile */
    nxcolor_profile_t *profile;
    
    /* Adjustments */
    float brightness;     /* 0.0 - 1.0 */
    float contrast;       /* 0.0 - 2.0 */
    float saturation;     /* 0.0 - 2.0 */
    int temperature;      /* -100 (cool) to +100 (warm) */
    
    /* Night mode */
    bool night_shift;
    int night_shift_start;  /* Hour (0-23) */
    int night_shift_end;
    float night_shift_warmth; /* 0.0 - 1.0 */
    
    /* HDR */
    bool hdr_enabled;
    float hdr_brightness;
    
    /* True Tone (ambient color matching) */
    bool true_tone;
} nxcolor_display_t;

/* ============ Color Management API ============ */

/* Initialize color management */
int nxcolor_init(void);
void nxcolor_shutdown(void);

/* Profile management */
nxcolor_profile_t *nxcolor_profile_load(const char *path);
nxcolor_profile_t *nxcolor_profile_get_default(nxcolor_space_t space);
void nxcolor_profile_free(nxcolor_profile_t *profile);
int nxcolor_profile_save(const nxcolor_profile_t *profile, const char *path);

/* List available profiles */
int nxcolor_profiles_list(nxcolor_profile_t **profiles, int max);
int nxcolor_profiles_system_count(void);
int nxcolor_profiles_user_count(void);

/* Display management */
int nxcolor_display_count(void);
nxcolor_display_t *nxcolor_display_get(int display_id);
int nxcolor_display_set_profile(int display_id, const nxcolor_profile_t *profile);
int nxcolor_display_calibrate(int display_id);

/* Color conversion */
typedef struct {
    float r, g, b, a;
} nxcolor_float_t;

typedef struct {
    float c, m, y, k;
} nxcolor_cmyk_t;

typedef struct {
    float l, a, b;
} nxcolor_lab_t;

/* Convert between color spaces */
nxcolor_float_t nxcolor_convert(nxcolor_float_t color,
                                 nxcolor_space_t from_space,
                                 nxcolor_space_t to_space);

nxcolor_cmyk_t nxcolor_rgb_to_cmyk(nxcolor_float_t rgb, const nxcolor_profile_t *profile);
nxcolor_float_t nxcolor_cmyk_to_rgb(nxcolor_cmyk_t cmyk, const nxcolor_profile_t *profile);
nxcolor_lab_t nxcolor_rgb_to_lab(nxcolor_float_t rgb);
nxcolor_float_t nxcolor_lab_to_rgb(nxcolor_lab_t lab);

/* Gamut mapping */
bool nxcolor_in_gamut(nxcolor_float_t color, nxcolor_space_t space);
nxcolor_float_t nxcolor_clamp_to_gamut(nxcolor_float_t color, nxcolor_space_t space);

/* ============ Rendering Intent ============ */

typedef enum {
    INTENT_PERCEPTUAL,       /* Best for photos */
    INTENT_RELATIVE,         /* Relative colorimetric */
    INTENT_SATURATION,       /* Best for graphics */
    INTENT_ABSOLUTE          /* Absolute colorimetric (proofing) */
} nxcolor_intent_t;

void nxcolor_set_rendering_intent(nxcolor_intent_t intent);
nxcolor_intent_t nxcolor_get_rendering_intent(void);

/* ============ System Color Preferences ============ */

typedef struct {
    /* Default working spaces */
    nxcolor_space_t rgb_working_space;
    nxcolor_space_t cmyk_working_space;
    
    /* Default rendering intent */
    nxcolor_intent_t default_intent;
    
    /* Black point compensation */
    bool black_point_compensation;
    
    /* Soft proofing */
    bool soft_proofing_enabled;
    nxcolor_profile_t *proof_profile;
    
    /* Gamut warning */
    bool gamut_warning_enabled;
    uint32_t gamut_warning_color;
} nxcolor_prefs_t;

int nxcolor_prefs_load(nxcolor_prefs_t *prefs);
int nxcolor_prefs_save(const nxcolor_prefs_t *prefs);

/* ============ ICC Profile Support ============ */

/* Load ICC profile */
nxcolor_profile_t *nxcolor_icc_load(const char *icc_path);
int nxcolor_icc_save(const nxcolor_profile_t *profile, const char *icc_path);

/* Embedded profile in image */
nxcolor_profile_t *nxcolor_extract_embedded(const char *image_path);
int nxcolor_embed_profile(const char *image_path, const nxcolor_profile_t *profile);

#endif /* NXCOLOR_H */
