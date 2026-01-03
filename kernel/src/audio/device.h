/*
 * NeolyxOS Audio Device Detection
 * 
 * Enumerate and detect all audio devices:
 * - PCI audio controllers (HDA, AC97)
 * - USB audio devices
 * - HDMI audio
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef KERNEL_AUDIO_DEVICE_H
#define KERNEL_AUDIO_DEVICE_H

#include "audio.h"

/* PCI device IDs */

/* Intel HDA controllers */
#define PCI_VID_INTEL           0x8086
#define PCI_DID_INTEL_ICH6      0x2668
#define PCI_DID_INTEL_ICH7      0x27D8
#define PCI_DID_INTEL_ICH8      0x284B
#define PCI_DID_INTEL_ICH9      0x293E
#define PCI_DID_INTEL_ICH10     0x3A3E
#define PCI_DID_INTEL_PCH       0x3B56
#define PCI_DID_INTEL_SERIES5   0x1C20
#define PCI_DID_INTEL_SERIES6   0x1E20
#define PCI_DID_INTEL_SERIES7   0x8C20
#define PCI_DID_INTEL_SERIES8   0x8CA0
#define PCI_DID_INTEL_CANNON    0xA348
#define PCI_DID_INTEL_TIGER     0xA0C8
#define PCI_DID_INTEL_ALDER     0x7AD0

/* AMD HDA controllers */
#define PCI_VID_AMD             0x1002
#define PCI_DID_AMD_SB450       0x437B
#define PCI_DID_AMD_SB600       0x4383
#define PCI_DID_AMD_SB700       0x780D
#define PCI_DID_AMD_HUDSON      0x780D
#define PCI_DID_AMD_RAVEN       0x15E3

/* NVIDIA HDA controllers */
#define PCI_VID_NVIDIA          0x10DE
#define PCI_DID_NVIDIA_MCP      0x044A
#define PCI_DID_NVIDIA_MCP55    0x055C
#define PCI_DID_NVIDIA_MCP65    0x07FC
#define PCI_DID_NVIDIA_TU102    0x10F7
#define PCI_DID_NVIDIA_GA102    0x228B

/* VIA AC97 */
#define PCI_VID_VIA             0x1106
#define PCI_DID_VIA_AC97        0x3058

/* Realtek codecs */
#define CODEC_VID_REALTEK       0x10EC
#define CODEC_DID_ALC262        0x0262
#define CODEC_DID_ALC269        0x0269
#define CODEC_DID_ALC272        0x0272
#define CODEC_DID_ALC282        0x0282
#define CODEC_DID_ALC892        0x0892
#define CODEC_DID_ALC1220       0x1220

/* Audio device detection info */
typedef struct {
    const char *name;
    uint16_t    vendor_id;
    uint16_t    device_id;
    uint32_t    type;
    uint32_t    caps;
} audio_device_match_t;

/* Detection functions */

/**
 * Scan PCI bus for audio devices
 * Called by audio_init()
 */
void audio_detect_pci_devices(void);

/**
 * Detect USB audio devices
 */
void audio_detect_usb_devices(void);

/**
 * Print detected devices
 */
void audio_print_devices(void);

/**
 * Get device by vendor/device ID
 */
audio_device_t* audio_find_device(uint16_t vendor, uint16_t device);

/**
 * Check if device is HDA controller
 */
int audio_is_hda_controller(uint16_t vendor, uint16_t device);

/**
 * Check if device is AC97 controller
 */
int audio_is_ac97_controller(uint16_t vendor, uint16_t device);

#endif /* KERNEL_AUDIO_DEVICE_H */
