/*
 * Rivee - Color Management Integration
 * Uses NeolyxOS NXColor for accurate color
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef RIVEE_COLOR_H
#define RIVEE_COLOR_H

#include "rivee.h"
#include <nxcolor.h>

/* ============ Document Color Settings ============ */

typedef struct {
    nxcolor_space_t color_space;     /* Working color space */
    nxcolor_profile_t *profile;       /* Embedded/assigned profile */
    nxcolor_intent_t rendering_intent;
    bool preserve_numbers;            /* Don't convert colors */
} rivee_color_settings_t;

/* ============ Color Picker ============ */

typedef enum {
    PICKER_MODE_RGB,
    PICKER_MODE_HSB,
    PICKER_MODE_HSL,
    PICKER_MODE_CMYK,
    PICKER_MODE_LAB,
    PICKER_MODE_HEX
} color_picker_mode_t;

typedef struct {
    rivee_color_t color;
    color_picker_mode_t mode;
    
    /* RGB values */
    uint8_t r, g, b, a;
    
    /* HSB values */
    float hue;        /* 0-360 */
    float saturation; /* 0-100 */
    float brightness; /* 0-100 */
    
    /* CMYK values */
    float c, m, y, k;
    
    /* Lab values */
    float lab_l, lab_a, lab_b;
    
    /* Hex string */
    char hex[10];
    
    /* Gamut warning */
    bool out_of_gamut;
    nxcolor_space_t target_gamut;
} rivee_color_picker_t;

/* ============ Color Functions ============ */

/* Initialize color picker */
void rivee_color_picker_init(rivee_color_picker_t *picker);
void rivee_color_picker_set_color(rivee_color_picker_t *picker, rivee_color_t color);
rivee_color_t rivee_color_picker_get_color(const rivee_color_picker_t *picker);

/* Update from different modes */
void rivee_color_picker_set_rgb(rivee_color_picker_t *picker, uint8_t r, uint8_t g, uint8_t b);
void rivee_color_picker_set_hsb(rivee_color_picker_t *picker, float h, float s, float b);
void rivee_color_picker_set_cmyk(rivee_color_picker_t *picker, float c, float m, float y, float k);
void rivee_color_picker_set_lab(rivee_color_picker_t *picker, float l, float a, float b);
void rivee_color_picker_set_hex(rivee_color_picker_t *picker, const char *hex);

/* Check gamut */
void rivee_color_picker_check_gamut(rivee_color_picker_t *picker, nxcolor_space_t space);

/* ============ Document Color Management ============ */

/* Assign profile to document */
int rivee_doc_assign_profile(rivee_document_t *doc, nxcolor_profile_t *profile);
int rivee_doc_convert_to_profile(rivee_document_t *doc, nxcolor_profile_t *profile);

/* Get document color mode */
nxcolor_space_t rivee_doc_get_color_space(const rivee_document_t *doc);

/* ============ Proofing ============ */

typedef struct {
    bool enabled;
    nxcolor_profile_t *proof_profile;
    nxcolor_intent_t intent;
    bool simulate_paper_color;
    bool simulate_black_ink;
    bool gamut_warning;
    rivee_color_t warning_color;
} rivee_proof_settings_t;

/* Soft proofing for print preview */
void rivee_proof_setup(const rivee_proof_settings_t *settings);
void rivee_proof_enable(bool enable);
bool rivee_proof_is_enabled(void);

/* ============ Color Harmonies ============ */

typedef enum {
    HARMONY_COMPLEMENTARY,   /* Opposite on wheel */
    HARMONY_ANALOGOUS,       /* Adjacent colors */
    HARMONY_TRIADIC,         /* 3 evenly spaced */
    HARMONY_SPLIT_COMP,      /* Split complementary */
    HARMONY_TETRADIC,        /* 4 colors (rectangle) */
    HARMONY_SQUARE           /* 4 colors (square) */
} color_harmony_t;

/* Generate harmony colors from base */
int rivee_color_harmony_generate(rivee_color_t base, color_harmony_t type,
                                  rivee_color_t *out, int max);

/* ============ Color Swatches ============ */

typedef struct {
    char name[32];
    rivee_color_t color;
    nxcolor_space_t space;
    bool global;        /* Global swatch (updates all uses) */
} rivee_swatch_t;

typedef struct {
    char name[64];
    rivee_swatch_t *swatches;
    int count;
} rivee_swatch_group_t;

/* Swatch management */
int rivee_swatches_add(rivee_swatch_group_t *group, const rivee_swatch_t *swatch);
int rivee_swatches_remove(rivee_swatch_group_t *group, int index);
int rivee_swatches_load(const char *path, rivee_swatch_group_t *group);
int rivee_swatches_save(const rivee_swatch_group_t *group, const char *path);

/* Create swatches from selection */
int rivee_swatches_from_selection(rivee_document_t *doc, rivee_swatch_group_t *group);

/* ============ Eyedropper ============ */

typedef struct {
    int sample_size;      /* 1x1, 3x3, 5x5, etc. */
    bool sample_all_layers;
    bool copy_to_clipboard;
} eyedropper_options_t;

rivee_color_t rivee_eyedropper_sample(rivee_document_t *doc, float x, float y,
                                       const eyedropper_options_t *options);

#endif /* RIVEE_COLOR_H */
