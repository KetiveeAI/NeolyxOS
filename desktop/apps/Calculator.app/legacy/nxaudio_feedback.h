/*
 * NeolyxOS Calculator.app - Audio Feedback
 * 
 * Provides audio feedback for button presses using NXAudio.
 * Includes click sounds, result tones, and error beeps.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#ifndef CALC_AUDIO_H
#define CALC_AUDIO_H

#include <stdint.h>

/* Audio feedback types */
typedef enum {
    AUDIO_CLICK_NUMBER,     /* Soft click for number buttons */
    AUDIO_CLICK_OPERATOR,   /* Medium click for operator buttons */
    AUDIO_CLICK_FUNCTION,   /* Click for function buttons */
    AUDIO_RESULT,           /* Success tone when equals pressed */
    AUDIO_ERROR,            /* Error beep for calculation errors */
    AUDIO_CLEAR,            /* Reset/clear sound */
    AUDIO_MEMORY            /* Memory operation sound */
} audio_feedback_t;

/* Haptic feedback types (for mobile devices) */
typedef enum {
    HAPTIC_LIGHT,           /* Light tap */
    HAPTIC_MEDIUM,          /* Medium tap */
    HAPTIC_HEAVY,           /* Strong tap */
    HAPTIC_SUCCESS,         /* Success pattern */
    HAPTIC_ERROR            /* Error pattern */
} haptic_feedback_t;

/* Initialize audio feedback system */
int audio_init(void);

/* Shutdown audio feedback system */
void audio_shutdown(void);

/* Play audio feedback */
void audio_play(audio_feedback_t type);

/* Trigger haptic feedback (mobile devices) */
void haptic_trigger(haptic_feedback_t type);

/* Set audio enabled/disabled */
void audio_set_enabled(int enabled);

/* Check if audio is enabled */
int audio_is_enabled(void);

/* Set audio volume (0.0 - 1.0) */
void audio_set_volume(float volume);

/* Convenience functions */
static inline void audio_play_click(void) { audio_play(AUDIO_CLICK_NUMBER); }
static inline void audio_play_operator(void) { audio_play(AUDIO_CLICK_OPERATOR); }
static inline void audio_play_result(void) { audio_play(AUDIO_RESULT); }
static inline void audio_play_error(void) { audio_play(AUDIO_ERROR); }
static inline void audio_play_clear(void) { audio_play(AUDIO_CLEAR); }

#endif /* CALC_AUDIO_H */
