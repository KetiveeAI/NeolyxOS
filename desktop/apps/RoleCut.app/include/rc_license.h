/*
 * RoleCut License System
 * 
 * LyxStore integration for edition management.
 * License stored in /System/Library/Licenses/
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef RC_LICENSE_H
#define RC_LICENSE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Edition Types (2 Editions)
 * ============================================================================ */

typedef enum {
    RC_EDITION_FREE = 0,        /* Full editor, 4K30, basic effects */
    RC_EDITION_PRO,             /* Extended, 4K60+, AI, advanced effects */
} rc_edition_t;

/* Edition names */
static const char *rc_edition_names[] = {
    "RoleCut",                  /* Free - no suffix, full product */
    "RoleCut Pro"
};

/* ============================================================================
 * Feature Flags
 * 
 * FREE: Full video editing, 4K@30fps, basic VFX (like After Effects basics)
 * PRO: Extended for creators - AI, motion tracking, advanced color grading
 * ============================================================================ */

typedef enum {
    /* Core (Both Editions) */
    RC_FEAT_TIMELINE            = (1 << 0),     /* Multi-track timeline */
    RC_FEAT_TRIM_SPLIT          = (1 << 1),     /* Trim, split, cut tools */
    RC_FEAT_TRANSITIONS_BASIC   = (1 << 2),     /* 10 basic transitions */
    RC_FEAT_EXPORT_4K30         = (1 << 3),     /* 4K @ 30fps export */
    
    /* Effects - Free (After Effects level basics) */
    RC_FEAT_COLOR_BASIC         = (1 << 4),     /* Brightness, contrast, exposure */
    RC_FEAT_BLUR_BASIC          = (1 << 5),     /* Gaussian, motion blur */
    RC_FEAT_STYLIZE_BASIC       = (1 << 6),     /* Vignette, B&W, sepia */
    RC_FEAT_TRANSFORM           = (1 << 7),     /* Position, scale, rotation */
    RC_FEAT_KEYFRAMES           = (1 << 8),     /* Basic keyframe animation */
    RC_FEAT_TEXT_TITLES         = (1 << 9),     /* Text and titles */
    RC_FEAT_SPEED_BASIC         = (1 << 10),    /* Speed change, reverse */
    
    /* Audio - Free (NXAudio enhanced) */
    RC_FEAT_AUDIO_MIX           = (1 << 11),    /* Multi-track audio */
    RC_FEAT_AUDIO_EQ            = (1 << 12),    /* Basic EQ */
    RC_FEAT_AUDIO_GAIN          = (1 << 13),    /* Volume, normalize */
    RC_FEAT_WAVEFORM            = (1 << 14),    /* Waveform display */
    
    /* Pro Only - Extended */
    RC_FEAT_EXPORT_4K60         = (1 << 15),    /* 4K @ 60fps+ */
    RC_FEAT_EXPORT_8K           = (1 << 16),    /* 8K export */
    RC_FEAT_TRANSITIONS_ALL     = (1 << 17),    /* All 18+ transitions */
    
    /* Pro - Advanced Color */
    RC_FEAT_COLOR_WHEELS        = (1 << 18),    /* 3-way color wheels */
    RC_FEAT_CURVES              = (1 << 19),    /* RGB curves */
    RC_FEAT_LUT_SUPPORT         = (1 << 20),    /* Import/apply LUTs */
    RC_FEAT_HSL_SECONDARY       = (1 << 21),    /* HSL secondary grading */
    
    /* Pro - Advanced VFX */
    RC_FEAT_CHROMA_KEY          = (1 << 22),    /* Green screen */
    RC_FEAT_MOTION_TRACKING     = (1 << 23),    /* Track objects */
    RC_FEAT_AI_MASK             = (1 << 24),    /* AI auto-masking */
    RC_FEAT_AI_DENOISE          = (1 << 25),    /* AI noise reduction */
    RC_FEAT_STABILIZE           = (1 << 26),    /* Video stabilization */
    
    /* Pro - Advanced Audio */
    RC_FEAT_AUDIO_SPATIAL       = (1 << 27),    /* Ketivee Oohm 3D audio */
    RC_FEAT_AUDIO_FX_ALL        = (1 << 28),    /* All audio effects */
    RC_FEAT_AUDIO_DUCKING       = (1 << 29),    /* Auto ducking */
    
    /* Pro - Motion & Camera */
    RC_FEAT_SPEED_RAMP          = (1 << 30),    /* Speed ramping curves */
    RC_FEAT_CAMERA_SHAKE        = (1u << 31),   /* Camera shake effects */
} rc_feature_t;

/* Feature sets per edition */
#define RC_FEATURES_FREE \
    (RC_FEAT_TIMELINE | RC_FEAT_TRIM_SPLIT | RC_FEAT_TRANSITIONS_BASIC | \
     RC_FEAT_EXPORT_4K30 | RC_FEAT_COLOR_BASIC | RC_FEAT_BLUR_BASIC | \
     RC_FEAT_STYLIZE_BASIC | RC_FEAT_TRANSFORM | RC_FEAT_KEYFRAMES | \
     RC_FEAT_TEXT_TITLES | RC_FEAT_SPEED_BASIC | RC_FEAT_AUDIO_MIX | \
     RC_FEAT_AUDIO_EQ | RC_FEAT_AUDIO_GAIN | RC_FEAT_WAVEFORM)

#define RC_FEATURES_PRO \
    (RC_FEATURES_FREE | RC_FEAT_EXPORT_4K60 | RC_FEAT_EXPORT_8K | \
     RC_FEAT_TRANSITIONS_ALL | RC_FEAT_COLOR_WHEELS | RC_FEAT_CURVES | \
     RC_FEAT_LUT_SUPPORT | RC_FEAT_HSL_SECONDARY | RC_FEAT_CHROMA_KEY | \
     RC_FEAT_MOTION_TRACKING | RC_FEAT_AI_MASK | RC_FEAT_AI_DENOISE | \
     RC_FEAT_STABILIZE | RC_FEAT_AUDIO_SPATIAL | RC_FEAT_AUDIO_FX_ALL | \
     RC_FEAT_AUDIO_DUCKING | RC_FEAT_SPEED_RAMP | RC_FEAT_CAMERA_SHAKE)

/* ============================================================================
 * License Info
 * ============================================================================ */

typedef struct {
    rc_edition_t edition;
    uint32_t features;          /* Bitmask of enabled features */
    
    /* User Info */
    char user_id[64];
    char user_email[128];
    
    /* License Details */
    char license_key[64];
    uint64_t activation_date;   /* Unix timestamp */
    uint64_t expiry_date;       /* 0 = perpetual */
    
    /* Status */
    bool is_valid;
    bool is_trial;
    int trial_days_left;
} rc_license_t;

/* ============================================================================
 * License Paths
 * ============================================================================ */

#define RC_LICENSE_SYSTEM_DIR   "/System/Library/Licenses"
#define RC_LICENSE_FILE         "com.neolyx.rolecut.lic"
#define RC_LICENSE_FULL_PATH    RC_LICENSE_SYSTEM_DIR "/" RC_LICENSE_FILE

/* ============================================================================
 * License API
 * ============================================================================ */

/**
 * Initialize license system and check current license.
 * @return Current edition
 */
rc_edition_t rc_license_init(void);

/**
 * Get current license info.
 */
const rc_license_t *rc_license_get(void);

/**
 * Check if current license has a specific feature.
 */
bool rc_license_has_feature(rc_feature_t feature);

/**
 * Get current edition.
 */
rc_edition_t rc_license_get_edition(void);

/**
 * Activate license from LyxStore.
 * @param license_key Key from purchase
 * @return true if activation succeeded
 */
bool rc_license_activate(const char *license_key);

/**
 * Deactivate current license.
 */
bool rc_license_deactivate(void);

/**
 * Start trial period (if not already used).
 * @return true if trial started
 */
bool rc_license_start_trial(void);

/**
 * Check if trial is available.
 */
bool rc_license_trial_available(void);

/**
 * Validate license with server (online check).
 */
bool rc_license_validate_online(void);

/* ============================================================================
 * Feature Gating Helpers
 * ============================================================================ */

/**
 * Check and show upgrade dialog if feature not available.
 * @return true if feature available, false if blocked
 */
bool rc_require_feature(rc_feature_t feature, const char *feature_name);

/**
 * Get maximum export resolution for current edition.
 */
int rc_license_max_export_height(void);

/**
 * Check if watermark should be shown (free edition).
 */
bool rc_license_show_watermark(void);

/* ============================================================================
 * UI Integration
 * ============================================================================ */

/**
 * Show upgrade dialog.
 */
void rc_license_show_upgrade_dialog(void);

/**
 * Show activation dialog.
 */
void rc_license_show_activation_dialog(void);

/**
 * Get edition badge text for UI.
 * e.g., "PRO" or "FREE"
 */
const char *rc_license_get_badge(void);

#ifdef __cplusplus
}
#endif

#endif /* RC_LICENSE_H */
