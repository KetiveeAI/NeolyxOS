/*
 * NeolyxOS NXAudio - Kernel Audio Subsystem
 * 
 * Public Brand: Ketivee Oohm
 * Internal: NXAudio kernel driver
 * 
 * Features:
 *   - Intel HDA (High Definition Audio)
 *   - Ketivee Oohm Spatial Audio (3D positioning, HRTF)
 *   - Multi-channel mixing with real-time effects
 *   - Echo/Reverb processing
 *   - Boot completion sound (NeolyxOS chime)
 *   - 5.1/7.1 Surround support
 *   - Dynamic audio balancing
 * 
 * "Sound: Ketivee Oohm"
 * 
 * Copyright (c) 2025 Ketivee Oohm / KetiveeAI.
 */

#ifndef NEOLYX_NXAUDIO_H
#define NEOLYX_NXAUDIO_H

#include <stdint.h>

/* ============ Audio Formats ============ */

typedef enum {
    AUDIO_FORMAT_PCM_U8,        /* Unsigned 8-bit */
    AUDIO_FORMAT_PCM_S16LE,     /* Signed 16-bit Little Endian */
    AUDIO_FORMAT_PCM_S24LE,     /* Signed 24-bit Little Endian */
    AUDIO_FORMAT_PCM_S32LE,     /* Signed 32-bit Little Endian */
    AUDIO_FORMAT_PCM_F32LE,     /* Float 32-bit Little Endian */
} audio_format_t;

/* ============ Sample Rates ============ */

#define AUDIO_RATE_8000     8000
#define AUDIO_RATE_11025    11025
#define AUDIO_RATE_22050    22050
#define AUDIO_RATE_44100    44100
#define AUDIO_RATE_48000    48000
#define AUDIO_RATE_96000    96000
#define AUDIO_RATE_192000   192000

/* ============ Audio Channel Config ============ */

typedef enum {
    AUDIO_CHANNELS_MONO = 1,
    AUDIO_CHANNELS_STEREO = 2,
    AUDIO_CHANNELS_SURROUND_51 = 6,
    AUDIO_CHANNELS_SURROUND_71 = 8,
} audio_channels_t;

/* ============ Output Types ============ */

typedef enum {
    AUDIO_OUTPUT_SPEAKERS = 0,
    AUDIO_OUTPUT_HEADPHONES,
    AUDIO_OUTPUT_HDMI,
    AUDIO_OUTPUT_SPDIF,
    AUDIO_OUTPUT_LINE_OUT,
} audio_output_t;

/* ============ Audio Stream ============ */

typedef struct {
    audio_format_t format;
    uint32_t sample_rate;
    uint8_t channels;
    
    /* Buffer */
    uint8_t *buffer;
    uint32_t buffer_size;
    uint32_t buffer_pos;
    
    /* State */
    int playing;
    int paused;
    int looping;
    
    /* Volume (0-100) */
    uint8_t volume;
} audio_stream_t;

/* ============ Intel HDA Controller ============ */

/* HDA Registers */
#define HDA_REG_GCAP        0x00    /* Global Capabilities */
#define HDA_REG_VMIN        0x02    /* Minor Version */
#define HDA_REG_VMAJ        0x03    /* Major Version */
#define HDA_REG_GCTL        0x08    /* Global Control */
#define HDA_REG_WAKEEN      0x0C    /* Wake Enable */
#define HDA_REG_STATESTS    0x0E    /* State Change Status */
#define HDA_REG_INTCTL      0x20    /* Interrupt Control */
#define HDA_REG_INTSTS      0x24    /* Interrupt Status */
#define HDA_REG_CORBLBASE   0x40    /* CORB Lower Base */
#define HDA_REG_CORBUBASE   0x44    /* CORB Upper Base */
#define HDA_REG_CORBWP      0x48    /* CORB Write Pointer */
#define HDA_REG_CORBRP      0x4A    /* CORB Read Pointer */
#define HDA_REG_CORBCTL     0x4C    /* CORB Control */
#define HDA_REG_RIRBSIZE    0x5E    /* RIRB Size */
#define HDA_REG_RIRBCTL     0x5C    /* RIRB Control */

/* Stream Descriptor Registers (offset from stream base) */
#define HDA_SD_CTL          0x00    /* Stream Control */
#define HDA_SD_STS          0x03    /* Stream Status */
#define HDA_SD_LPIB         0x04    /* Link Position in Buffer */
#define HDA_SD_CBL          0x08    /* Cyclic Buffer Length */
#define HDA_SD_LVI          0x0C    /* Last Valid Index */
#define HDA_SD_FMT          0x12    /* Format */
#define HDA_SD_BDPL         0x18    /* Buffer Descriptor List Pointer Low */
#define HDA_SD_BDPU         0x1C    /* Buffer Descriptor List Pointer High */

/* HDA Device IDs */
#define PCI_VENDOR_INTEL    0x8086
#define PCI_VENDOR_AMD      0x1002
#define PCI_VENDOR_NVIDIA   0x10DE
#define PCI_VENDOR_REALTEK  0x10EC

/* Intel HDA controllers */
#define INTEL_HDA_ICH6      0x2668
#define INTEL_HDA_ICH7      0x27D8
#define INTEL_HDA_PCH       0x1C20
#define INTEL_HDA_SUNRISE   0xA170
#define INTEL_HDA_CANNONLAKE 0xA348

/* ============ Audio Device ============ */

typedef struct audio_device {
    char name[64];
    uint16_t vendor_id;
    uint16_t device_id;
    
    /* PCI */
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    
    /* MMIO */
    volatile uint8_t *mmio_base;
    
    /* CORB/RIRB */
    uint32_t *corb;
    uint64_t *rirb;
    
    /* Streams */
    int num_output_streams;
    int num_input_streams;
    
    /* Current config */
    uint32_t sample_rate;
    uint8_t channels;
    audio_format_t format;
    
    /* Master volume (0-100) */
    uint8_t master_volume;
    int muted;
    
    /* Active output */
    audio_output_t active_output;
    
    /* Playback buffer */
    uint8_t *playback_buffer;
    uint32_t playback_size;
    uint32_t playback_pos;
    
    struct audio_device *next;
} audio_device_t;

/* ============ NXAudio API ============ */

/**
 * Initialize NXAudio subsystem.
 */
int nxaudio_init(void);

/**
 * Get primary audio device.
 */
audio_device_t *nxaudio_get_device(void);

/**
 * Get device by index.
 */
audio_device_t *nxaudio_get_device_by_index(int index);

/**
 * Get number of audio devices.
 */
int nxaudio_device_count(void);

/* ============ Playback ============ */

/**
 * Play audio buffer.
 */
int nxaudio_play(audio_device_t *dev, const void *data, uint32_t len,
                 uint32_t sample_rate, uint8_t channels, audio_format_t format);

/**
 * Stop playback.
 */
int nxaudio_stop(audio_device_t *dev);

/**
 * Pause playback.
 */
int nxaudio_pause(audio_device_t *dev);

/**
 * Resume playback.
 */
int nxaudio_resume(audio_device_t *dev);

/**
 * Check if playing.
 */
int nxaudio_is_playing(audio_device_t *dev);

/* ============ Volume ============ */

/**
 * Set master volume (0-100).
 */
int nxaudio_set_volume(audio_device_t *dev, uint8_t volume);

/**
 * Get master volume.
 */
uint8_t nxaudio_get_volume(audio_device_t *dev);

/**
 * Mute/unmute.
 */
int nxaudio_set_mute(audio_device_t *dev, int muted);

/* ============ Output ============ */

/**
 * Set active output.
 */
int nxaudio_set_output(audio_device_t *dev, audio_output_t output);

/**
 * Get available outputs.
 */
int nxaudio_get_outputs(audio_device_t *dev, audio_output_t *outputs, int max);

/* ============ Boot Sound ============ */

/**
 * Play the NeolyxOS boot completion chime.
 * Called when OS is ready.
 */
void nxaudio_play_boot_sound(void);

/**
 * Generate boot sound waveform.
 */
void nxaudio_generate_boot_sound(int16_t *buffer, uint32_t *samples);

/* ============ System Sounds ============ */

typedef enum {
    SYSTEM_SOUND_BOOT,          /* Boot complete */
    SYSTEM_SOUND_SHUTDOWN,      /* Shutdown */
    SYSTEM_SOUND_ERROR,         /* Error beep */
    SYSTEM_SOUND_NOTIFICATION,  /* Notification */
    SYSTEM_SOUND_CLICK,         /* UI click */
} system_sound_t;

/**
 * Play a system sound.
 */
int nxaudio_play_system_sound(system_sound_t sound);

/* ============================================================
 * KETIVEE OOHM SPATIAL AUDIO ENGINE
 * 
 * 3D spatial audio with HRTF (Head-Related Transfer Function),
 * real-time effects, multi-channel mixing, and audio balancing.
 * 
 * "Boot chime powered by Ketivee Oohm"
 * ============================================================ */

/* ============ Spatial Audio Position ============ */

typedef struct {
    float x;        /* Left/Right: -1.0 (left) to 1.0 (right) */
    float y;        /* Up/Down: -1.0 (down) to 1.0 (up) */
    float z;        /* Front/Back: -1.0 (behind) to 1.0 (front) */
} nxaudio_spatial_position_t;

/* ============ Audio Source (for 3D mixing) ============ */

typedef struct {
    uint32_t id;                /* Unique source ID */
    nxaudio_spatial_position_t position;   /* 3D position */
    float volume;               /* Source volume 0.0-1.0 */
    float pitch;                /* Pitch multiplier (1.0 = normal) */
    float distance;             /* Distance from listener */
    float rolloff;              /* Distance attenuation factor */
    int active;                 /* Is source playing? */
} nxaudio_spatial_source_t;

/* ============ Listener (ear position) ============ */

typedef struct {
    nxaudio_spatial_position_t position;   /* Listener position */
    nxaudio_spatial_position_t forward;    /* Look direction */
    nxaudio_spatial_position_t up;         /* Up vector */
} nxaudio_spatial_listener_t;

/* ============ HRTF (Head-Related Transfer Function) ============ */

typedef struct {
    int16_t left_ir[128];       /* Left ear impulse response */
    int16_t right_ir[128];      /* Right ear impulse response */
    float azimuth;              /* Horizontal angle */
    float elevation;            /* Vertical angle */
} nxaudio_spatial_hrtf_t;

/* ============ Echo/Reverb Effect ============ */

typedef struct {
    float delay_ms;             /* Echo delay in milliseconds */
    float decay;                /* Decay factor 0.0-1.0 */
    float wet_mix;              /* Effect mix 0.0-1.0 */
    float room_size;            /* Virtual room size */
    float damping;              /* High-frequency damping */
    int enabled;
} nxaudio_spatial_reverb_t;

/* ============ Equalizer ============ */

typedef struct {
    float bass;                 /* Bass gain -12 to +12 dB */
    float mid;                  /* Mid gain -12 to +12 dB */
    float treble;               /* Treble gain -12 to +12 dB */
    float presence;             /* Presence (2-5kHz) */
} nxaudio_spatial_eq_t;

/* ============ Audio Balancer ============ */

typedef struct {
    float left_right;           /* L/R balance: -1.0 (left) to 1.0 (right) */
    float front_rear;           /* F/R balance: -1.0 (rear) to 1.0 (front) */
    float center_level;         /* Center channel level */
    float subwoofer_level;      /* LFE/Sub level */
} nxaudio_spatial_balance_t;

/* ============ Spatial Audio Context ============ */

#define NXAUDIO_SPATIAL_MAX_SOURCES 32

typedef struct {
    nxaudio_spatial_listener_t listener;
    nxaudio_spatial_source_t sources[NXAUDIO_SPATIAL_MAX_SOURCES];
    int source_count;
    
    nxaudio_spatial_reverb_t reverb;
    nxaudio_spatial_eq_t eq;
    nxaudio_spatial_balance_t balance;
    
    /* HRTF database */
    nxaudio_spatial_hrtf_t *hrtf_data;
    int hrtf_count;
    
    /* Processing state */
    int enabled;
    int surround_mode;          /* 0=stereo, 1=5.1, 2=7.1 */
    float master_gain;
} nxaudio_spatial_context_t;

/* ============ Ketivee Oohm Initialization ============ */

/**
 * Initialize Ketivee Oohm spatial audio engine.
 */
int nxaudio_spatial_init(void);

/**
 * Get the spatial audio context.
 */
nxaudio_spatial_context_t *nxaudio_spatial_get_context(void);

/**
 * Enable/disable spatial audio processing.
 */
void nxaudio_spatial_set_enabled(int enabled);

/* ============ Source Management ============ */

/**
 * Create a new 3D audio source.
 * @return Source ID or -1 on error
 */
int nxaudio_spatial_create_source(void);

/**
 * Destroy an audio source.
 */
void nxaudio_spatial_destroy_source(int source_id);

/**
 * Set source 3D position.
 */
void nxaudio_spatial_set_source_position(int source_id, float x, float y, float z);

/**
 * Set source volume.
 */
void nxaudio_spatial_set_source_volume(int source_id, float volume);

/**
 * Play audio through a positioned source.
 */
int nxaudio_spatial_source_play(int source_id, const void *data, uint32_t len,
                     uint32_t sample_rate, audio_format_t format);

/* ============ Listener ============ */

/**
 * Set listener (ear) position.
 */
void nxaudio_spatial_set_listener_position(float x, float y, float z);

/**
 * Set listener orientation.
 */
void nxaudio_spatial_set_listener_orientation(float fx, float fy, float fz,
                                    float ux, float uy, float uz);

/* ============ Effects ============ */

/**
 * Configure reverb/echo effect.
 */
void nxaudio_spatial_set_reverb(float delay_ms, float decay, float wet_mix);

/**
 * Enable/disable reverb.
 */
void nxaudio_spatial_enable_reverb(int enabled);

/**
 * Set equalizer levels.
 */
void nxaudio_spatial_set_eq(float bass, float mid, float treble);

/* ============ Audio Balancing ============ */

/**
 * Set left/right balance.
 * @param balance -1.0 (full left) to 1.0 (full right)
 */
void nxaudio_spatial_set_balance(float balance);

/**
 * Set front/rear balance for surround.
 */
void nxaudio_spatial_set_fade(float fade);

/**
 * Set surround mode.
 * @param mode 0=stereo, 1=5.1, 2=7.1
 */
void nxaudio_spatial_set_surround_mode(int mode);

/* ============ HRTF Processing ============ */

/**
 * Apply HRTF to stereo buffer for 3D effect.
 * Processes audio to simulate sound coming from a 3D position.
 */
void nxaudio_spatial_apply_hrtf(int16_t *buffer, uint32_t samples,
                     float azimuth, float elevation);

/**
 * Mix all active 3D sources to stereo output.
 */
int nxaudio_spatial_mix_3d_sources(int16_t *output, uint32_t samples);

/* ============ Ketivee Oohm Branding ============ */

/**
 * Get Ketivee Oohm version string.
 */
const char *nxaudio_spatial_get_version(void);

/**
 * Get branding string for UI display.
 * Returns: "Sound: Ketivee Oohm"
 */
const char *nxaudio_spatial_get_brand(void);

#endif /* NEOLYX_NXAUDIO_H */
