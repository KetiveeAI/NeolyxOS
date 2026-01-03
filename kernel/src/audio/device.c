/*
 * NeolyxOS Audio Device Detection Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "device.h"
#include "audio.h"
#include "hda_driver.h"
#include "../drivers/pci/pci.h"
#include "../serial.h"
#include <string.h>

/* Device match table */
static const audio_device_match_t device_table[] = {
    /* Intel HDA */
    {"Intel ICH6 HDA", PCI_VID_INTEL, PCI_DID_INTEL_ICH6, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel ICH7 HDA", PCI_VID_INTEL, PCI_DID_INTEL_ICH7, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel ICH8 HDA", PCI_VID_INTEL, PCI_DID_INTEL_ICH8, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel ICH9 HDA", PCI_VID_INTEL, PCI_DID_INTEL_ICH9, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel ICH10 HDA", PCI_VID_INTEL, PCI_DID_INTEL_ICH10, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel PCH HDA", PCI_VID_INTEL, PCI_DID_INTEL_PCH, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel Series 5 HDA", PCI_VID_INTEL, PCI_DID_INTEL_SERIES5, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel Series 6 HDA", PCI_VID_INTEL, PCI_DID_INTEL_SERIES6, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel Series 7 HDA", PCI_VID_INTEL, PCI_DID_INTEL_SERIES7, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel Series 8 HDA", PCI_VID_INTEL, PCI_DID_INTEL_SERIES8, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel Cannon Lake HDA", PCI_VID_INTEL, PCI_DID_INTEL_CANNON, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel Tiger Lake HDA", PCI_VID_INTEL, PCI_DID_INTEL_TIGER, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"Intel Alder Lake HDA", PCI_VID_INTEL, PCI_DID_INTEL_ALDER, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    
    /* AMD HDA */
    {"AMD SB450 HDA", PCI_VID_AMD, PCI_DID_AMD_SB450, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"AMD SB600 HDA", PCI_VID_AMD, PCI_DID_AMD_SB600, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"AMD SB700 HDA", PCI_VID_AMD, PCI_DID_AMD_SB700, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    {"AMD Raven HDA", PCI_VID_AMD, PCI_DID_AMD_RAVEN, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    
    /* NVIDIA HDA */
    {"NVIDIA MCP HDA", PCI_VID_NVIDIA, PCI_DID_NVIDIA_MCP, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK},
    {"NVIDIA MCP55 HDA", PCI_VID_NVIDIA, PCI_DID_NVIDIA_MCP55, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK},
    {"NVIDIA MCP65 HDA", PCI_VID_NVIDIA, PCI_DID_NVIDIA_MCP65, AUDIO_DEV_TYPE_HDA, AUDIO_CAP_PLAYBACK},
    {"NVIDIA TU102 HDA", PCI_VID_NVIDIA, PCI_DID_NVIDIA_TU102, AUDIO_DEV_TYPE_HDMI, AUDIO_CAP_PLAYBACK},
    {"NVIDIA GA102 HDA", PCI_VID_NVIDIA, PCI_DID_NVIDIA_GA102, AUDIO_DEV_TYPE_HDMI, AUDIO_CAP_PLAYBACK},
    
    /* VIA AC97 */
    {"VIA AC97", PCI_VID_VIA, PCI_DID_VIA_AC97, AUDIO_DEV_TYPE_AC97, AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE},
    
    /* Terminator */
    {NULL, 0, 0, 0, 0}
};

/* Find device in table */
static const audio_device_match_t* find_device_match(uint16_t vendor, uint16_t device) {
    for (int i = 0; device_table[i].name != NULL; i++) {
        if (device_table[i].vendor_id == vendor && 
            device_table[i].device_id == device) {
            return &device_table[i];
        }
    }
    return NULL;
}

/* Logging */
static void device_log(const char *msg) {
    serial_puts("[AUDIO-DEV] ");
    serial_puts(msg);
    serial_puts("\n");
}

/* Scan PCI for audio devices */
void audio_detect_pci_devices(void) {
    device_log("Scanning PCI bus for audio devices");
    
    int pci_count = pci_get_device_count();
    int found = 0;
    
    for (int i = 0; i < pci_count; i++) {
        pci_device_t *pci = pci_get_device(i);
        if (!pci) continue;
        
        uint8_t class = (pci->class_code >> 16) & 0xFF;
        
        /* Multimedia device */
        if (class == 0x04) {
            const audio_device_match_t *match = find_device_match(
                pci->vendor_id, pci->device_id);
            
            audio_device_t dev;
            memset(&dev, 0, sizeof(dev));
            
            if (match) {
                strcpy(dev.info.name, match->name);
                dev.info.type = match->type;
                dev.info.caps = match->caps;
            } else {
                /* Generic detection by subclass */
                uint8_t subclass = (pci->class_code >> 8) & 0xFF;
                
                if (subclass == 0x03) {
                    strcpy(dev.info.name, "Generic HD Audio");
                    dev.info.type = AUDIO_DEV_TYPE_HDA;
                    dev.info.caps = AUDIO_CAP_PLAYBACK;
                } else if (subclass == 0x01) {
                    strcpy(dev.info.name, "Generic Audio");
                    dev.info.type = AUDIO_DEV_TYPE_AC97;
                    dev.info.caps = AUDIO_CAP_PLAYBACK;
                } else {
                    continue;
                }
            }
            
            /* Set vendor name */
            switch (pci->vendor_id) {
                case 0x8086: strcpy(dev.info.vendor, "Intel"); break;
                case 0x1002: strcpy(dev.info.vendor, "AMD"); break;
                case 0x10DE: strcpy(dev.info.vendor, "NVIDIA"); break;
                case 0x1106: strcpy(dev.info.vendor, "VIA"); break;
                case 0x10EC: strcpy(dev.info.vendor, "Realtek"); break;
                case 0x14F1: strcpy(dev.info.vendor, "Conexant"); break;
                case 0x1013: strcpy(dev.info.vendor, "Cirrus Logic"); break;
                default:     strcpy(dev.info.vendor, "Unknown"); break;
            }
            
            /* Set hardware info */
            dev.mmio_base = (void*)(uintptr_t)(pci->bar[0] & ~0xF);
            dev.irq = pci->irq;
            
            /* Set format capabilities */
            if (dev.info.type == AUDIO_DEV_TYPE_HDA) {
                dev.info.min_rate = 8000;
                dev.info.max_rate = 192000;
                dev.info.min_channels = 1;
                dev.info.max_channels = 8;
                dev.info.formats = (1 << AUDIO_FMT_S16_LE) |
                                   (1 << AUDIO_FMT_S24_LE) |
                                   (1 << AUDIO_FMT_S32_LE);
            } else if (dev.info.type == AUDIO_DEV_TYPE_AC97) {
                dev.info.min_rate = 8000;
                dev.info.max_rate = 48000;
                dev.info.min_channels = 1;
                dev.info.max_channels = 2;
                dev.info.formats = (1 << AUDIO_FMT_S16_LE);
            }
            
            /* Register device */
            audio_register_device(&dev);
            found++;
            
            char msg[64];
            snprintf(msg, sizeof(msg), "Found: %s (%s)", 
                     dev.info.name, dev.info.vendor);
            device_log(msg);
        }
    }
    
    char buf[32];
    snprintf(buf, sizeof(buf), "Total devices found: %d", found);
    device_log(buf);
}

void audio_detect_usb_devices(void) {
    /* USB audio detection - placeholder */
    device_log("USB audio detection not implemented");
}

void audio_print_devices(void) {
    int count = audio_get_device_count();
    
    serial_puts("\n--- Audio Devices ---\n");
    
    for (int i = 0; i < count; i++) {
        audio_device_info_t info;
        if (audio_get_device_info(i, &info) == AUDIO_OK) {
            char buf[128];
            snprintf(buf, sizeof(buf), 
                     "[%d] %s (%s)\n    Type: %s, Rate: %d-%d Hz, Ch: %d-%d\n",
                     i, info.name, info.vendor,
                     (info.type == AUDIO_DEV_TYPE_HDA) ? "HDA" :
                     (info.type == AUDIO_DEV_TYPE_AC97) ? "AC97" :
                     (info.type == AUDIO_DEV_TYPE_HDMI) ? "HDMI" : "Unknown",
                     info.min_rate, info.max_rate,
                     info.min_channels, info.max_channels);
            serial_puts(buf);
        }
    }
    
    serial_puts("---------------------\n\n");
}

audio_device_t* audio_find_device(uint16_t vendor, uint16_t device) {
    int count = audio_get_device_count();
    
    for (int i = 0; i < count; i++) {
        audio_device_t *dev = audio_get_device(i);
        /* Would need to store PCI IDs in device struct for proper matching */
        (void)vendor; (void)device;
        if (dev) return dev;
    }
    
    return NULL;
}

int audio_is_hda_controller(uint16_t vendor, uint16_t device) {
    const audio_device_match_t *match = find_device_match(vendor, device);
    return (match && match->type == AUDIO_DEV_TYPE_HDA);
}

int audio_is_ac97_controller(uint16_t vendor, uint16_t device) {
    const audio_device_match_t *match = find_device_match(vendor, device);
    return (match && match->type == AUDIO_DEV_TYPE_AC97);
}
