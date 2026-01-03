/*
 * NeolyxOS Kernel Audio Subsystem Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "audio.h"
#include "../drivers/pci/pci.h"
#include "../serial.h"
#include <string.h>

/* PCI Class codes for audio */
#define PCI_CLASS_MULTIMEDIA        0x04
#define PCI_SUBCLASS_AUDIO          0x01
#define PCI_SUBCLASS_HDA            0x03

/* Global state */
static struct {
    int             initialized;
    audio_device_t  devices[AUDIO_MAX_DEVICES];
    int             device_count;
    audio_driver_t *drivers[8];
    int             driver_count;
} g_audio = {0};

/* Serial logging */
static void audio_log(const char *msg) {
    serial_puts("[AUDIO] ");
    serial_puts(msg);
    serial_puts("\n");
}

/* Device detection via PCI */
static void audio_scan_pci(void) {
    audio_log("Scanning PCI for audio devices...");
    
    int pci_count = pci_get_device_count();
    
    for (int i = 0; i < pci_count && g_audio.device_count < AUDIO_MAX_DEVICES; i++) {
        pci_device_t *pci = pci_get_device(i);
        if (!pci) continue;
        
        uint8_t class = (pci->class_code >> 16) & 0xFF;
        uint8_t subclass = (pci->class_code >> 8) & 0xFF;
        
        if (class == PCI_CLASS_MULTIMEDIA) {
            audio_device_t *dev = &g_audio.devices[g_audio.device_count];
            memset(dev, 0, sizeof(*dev));
            
            dev->id = g_audio.device_count;
            dev->mmio_base = (void*)(uintptr_t)pci->bar[0];
            dev->irq = pci->irq;
            
            if (subclass == PCI_SUBCLASS_HDA) {
                dev->info.type = AUDIO_DEV_TYPE_HDA;
                strcpy(dev->info.name, "Intel HD Audio");
                dev->info.caps = AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE;
                dev->info.min_rate = 8000;
                dev->info.max_rate = 192000;
                dev->info.min_channels = 1;
                dev->info.max_channels = 8;
                dev->info.formats = (1 << AUDIO_FMT_S16_LE) | 
                                    (1 << AUDIO_FMT_S24_LE) |
                                    (1 << AUDIO_FMT_S32_LE);
                audio_log("Found HD Audio controller");
            }
            else if (subclass == PCI_SUBCLASS_AUDIO) {
                dev->info.type = AUDIO_DEV_TYPE_AC97;
                strcpy(dev->info.name, "AC97 Audio");
                dev->info.caps = AUDIO_CAP_PLAYBACK | AUDIO_CAP_CAPTURE;
                dev->info.min_rate = 8000;
                dev->info.max_rate = 48000;
                dev->info.min_channels = 1;
                dev->info.max_channels = 2;
                dev->info.formats = (1 << AUDIO_FMT_S16_LE);
                audio_log("Found AC97 controller");
            }
            else {
                dev->info.type = AUDIO_DEV_TYPE_UNKNOWN;
                strcpy(dev->info.name, "Unknown Audio");
                dev->info.caps = AUDIO_CAP_PLAYBACK;
            }
            
            /* Get vendor name */
            switch (pci->vendor_id) {
                case 0x8086: strcpy(dev->info.vendor, "Intel"); break;
                case 0x1002: strcpy(dev->info.vendor, "AMD"); break;
                case 0x10DE: strcpy(dev->info.vendor, "NVIDIA"); break;
                case 0x1022: strcpy(dev->info.vendor, "AMD"); break;
                case 0x1106: strcpy(dev->info.vendor, "VIA"); break;
                case 0x10EC: strcpy(dev->info.vendor, "Realtek"); break;
                default:     strcpy(dev->info.vendor, "Unknown"); break;
            }
            
            g_audio.device_count++;
        }
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Found %d audio device(s)", g_audio.device_count);
    audio_log(buf);
}

/* Try to attach driver to device */
static int audio_attach_driver(audio_device_t *dev) {
    for (int i = 0; i < g_audio.driver_count; i++) {
        audio_driver_t *drv = g_audio.drivers[i];
        
        if (drv->type == dev->info.type && drv->ops && drv->ops->probe) {
            if (drv->ops->probe(dev) == 0) {
                dev->driver = drv;
                audio_log("Driver attached");
                
                if (drv->ops->init) {
                    drv->ops->init(dev);
                }
                return 0;
            }
        }
    }
    return -1;
}

/* Public API */

int audio_init(void) {
    if (g_audio.initialized) {
        return AUDIO_OK;
    }
    
    audio_log("Initializing audio subsystem");
    
    memset(&g_audio, 0, sizeof(g_audio));
    
    /* Scan for devices */
    audio_scan_pci();
    
    /* Try to attach drivers */
    for (int i = 0; i < g_audio.device_count; i++) {
        audio_attach_driver(&g_audio.devices[i]);
    }
    
    g_audio.initialized = 1;
    audio_log("Audio subsystem ready");
    
    return AUDIO_OK;
}

void audio_cleanup(void) {
    if (!g_audio.initialized) return;
    
    audio_log("Cleaning up audio subsystem");
    
    for (int i = 0; i < g_audio.device_count; i++) {
        audio_device_t *dev = &g_audio.devices[i];
        if (dev->opened) {
            audio_device_close(dev);
        }
        if (dev->driver && dev->driver->ops && dev->driver->ops->remove) {
            dev->driver->ops->remove(dev);
        }
    }
    
    memset(&g_audio, 0, sizeof(g_audio));
}

int audio_get_device_count(void) {
    return g_audio.device_count;
}

int audio_get_device_info(int index, audio_device_info_t *info) {
    if (index < 0 || index >= g_audio.device_count || !info) {
        return AUDIO_ERR_INVALID;
    }
    
    *info = g_audio.devices[index].info;
    return AUDIO_OK;
}

audio_device_t* audio_get_device(int index) {
    if (index < 0 || index >= g_audio.device_count) {
        return NULL;
    }
    return &g_audio.devices[index];
}

audio_device_t* audio_get_default_device(void) {
    /* Return first HDA device, or first available */
    for (int i = 0; i < g_audio.device_count; i++) {
        if (g_audio.devices[i].info.type == AUDIO_DEV_TYPE_HDA) {
            return &g_audio.devices[i];
        }
    }
    
    if (g_audio.device_count > 0) {
        return &g_audio.devices[0];
    }
    
    return NULL;
}

int audio_device_open(audio_device_t *dev, audio_stream_config_t *config) {
    if (!dev || !config) return AUDIO_ERR_INVALID;
    if (dev->opened) return AUDIO_ERR_BUSY;
    
    dev->config = *config;
    
    if (dev->driver && dev->driver->ops && dev->driver->ops->open) {
        int ret = dev->driver->ops->open(dev, config);
        if (ret != 0) return ret;
    }
    
    dev->opened = 1;
    dev->write_pos = 0;
    dev->read_pos = 0;
    
    return AUDIO_OK;
}

void audio_device_close(audio_device_t *dev) {
    if (!dev || !dev->opened) return;
    
    if (dev->running) {
        audio_device_stop(dev);
    }
    
    if (dev->driver && dev->driver->ops && dev->driver->ops->close) {
        dev->driver->ops->close(dev);
    }
    
    dev->opened = 0;
}

int audio_device_start(audio_device_t *dev) {
    if (!dev || !dev->opened) return AUDIO_ERR_INVALID;
    if (dev->running) return AUDIO_OK;
    
    if (dev->driver && dev->driver->ops && dev->driver->ops->start) {
        int ret = dev->driver->ops->start(dev);
        if (ret != 0) return ret;
    }
    
    dev->running = 1;
    return AUDIO_OK;
}

int audio_device_stop(audio_device_t *dev) {
    if (!dev) return AUDIO_ERR_INVALID;
    if (!dev->running) return AUDIO_OK;
    
    if (dev->driver && dev->driver->ops && dev->driver->ops->stop) {
        dev->driver->ops->stop(dev);
    }
    
    dev->running = 0;
    return AUDIO_OK;
}

int audio_write(audio_device_t *dev, const void *data, size_t frames) {
    if (!dev || !data || !dev->opened) return AUDIO_ERR_INVALID;
    
    if (dev->driver && dev->driver->ops && dev->driver->ops->write) {
        return dev->driver->ops->write(dev, data, frames);
    }
    
    return AUDIO_ERR_IO;
}

int audio_read(audio_device_t *dev, void *data, size_t frames) {
    if (!dev || !data || !dev->opened) return AUDIO_ERR_INVALID;
    
    if (dev->driver && dev->driver->ops && dev->driver->ops->read) {
        return dev->driver->ops->read(dev, data, frames);
    }
    
    return AUDIO_ERR_IO;
}

int audio_set_volume(audio_device_t *dev, int left, int right) {
    if (!dev) return AUDIO_ERR_INVALID;
    
    if (dev->driver && dev->driver->ops && dev->driver->ops->set_volume) {
        return dev->driver->ops->set_volume(dev, left, right);
    }
    
    return AUDIO_ERR_IO;
}

int audio_get_volume(audio_device_t *dev, int *left, int *right) {
    if (!dev || !left || !right) return AUDIO_ERR_INVALID;
    
    if (dev->driver && dev->driver->ops && dev->driver->ops->get_volume) {
        return dev->driver->ops->get_volume(dev, left, right);
    }
    
    return AUDIO_ERR_IO;
}

int audio_register_driver(audio_driver_t *driver) {
    if (!driver || g_audio.driver_count >= 8) {
        return AUDIO_ERR_INVALID;
    }
    
    g_audio.drivers[g_audio.driver_count++] = driver;
    
    audio_log("Driver registered: ");
    audio_log(driver->name);
    
    /* Try to attach to existing devices */
    for (int i = 0; i < g_audio.device_count; i++) {
        if (!g_audio.devices[i].driver) {
            audio_attach_driver(&g_audio.devices[i]);
        }
    }
    
    return AUDIO_OK;
}

void audio_unregister_driver(audio_driver_t *driver) {
    if (!driver) return;
    
    for (int i = 0; i < g_audio.driver_count; i++) {
        if (g_audio.drivers[i] == driver) {
            /* Remove from devices */
            for (int j = 0; j < g_audio.device_count; j++) {
                if (g_audio.devices[j].driver == driver) {
                    g_audio.devices[j].driver = NULL;
                }
            }
            
            /* Shift array */
            for (int k = i; k < g_audio.driver_count - 1; k++) {
                g_audio.drivers[k] = g_audio.drivers[k + 1];
            }
            g_audio.driver_count--;
            break;
        }
    }
}

int audio_register_device(audio_device_t *dev) {
    if (!dev || g_audio.device_count >= AUDIO_MAX_DEVICES) {
        return AUDIO_ERR_INVALID;
    }
    
    dev->id = g_audio.device_count;
    g_audio.devices[g_audio.device_count++] = *dev;
    
    return AUDIO_OK;
}

void audio_unregister_device(audio_device_t *dev) {
    if (!dev) return;
    
    for (int i = 0; i < g_audio.device_count; i++) {
        if (g_audio.devices[i].id == dev->id) {
            if (g_audio.devices[i].opened) {
                audio_device_close(&g_audio.devices[i]);
            }
            
            for (int k = i; k < g_audio.device_count - 1; k++) {
                g_audio.devices[k] = g_audio.devices[k + 1];
            }
            g_audio.device_count--;
            break;
        }
    }
}
