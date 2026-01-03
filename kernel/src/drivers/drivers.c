/* Include from kernel/include for full struct definitions */
#include "../../include/drivers/drivers.h"
#include <core/kernel.h>
#include <video/video.h>
#include <utils/string.h>
#include <drivers/ethernet.h>
#include <drivers/wifi.h>
#include <drivers/ata.h>
#include <drivers/mouse.h>
#include <utils/log.h>

// Global driver list
static kernel_driver_t* driver_list = NULL;
static int driver_count = 0;

// Driver instances
static kernel_driver_t ethernet_driver = {
    .name = "ethernet",
    .type = DRIVER_NETWORK_ETHERNET,
    .status = DRIVER_STATUS_UNLOADED,
    .init = ethernet_driver_init,
    .deinit = ethernet_driver_deinit,
    .next = NULL
};

static kernel_driver_t wifi_driver = {
    .name = "wifi",
    .type = DRIVER_NETWORK_WIFI,
    .status = DRIVER_STATUS_UNLOADED,
    .init = wifi_driver_init,
    .deinit = wifi_driver_deinit,
    .next = NULL
};

static kernel_driver_t ata_driver = {
    .name = "ata",
    .type = DRIVER_STORAGE_DISK,
    .status = DRIVER_STATUS_UNLOADED,
    .init = ata_driver_init,
    .deinit = ata_driver_deinit,
    .next = NULL
};

static kernel_driver_t mouse_driver = {
    .name = "mouse",
    .type = DRIVER_INPUT_MOUSE,
    .status = DRIVER_STATUS_UNLOADED,
    .init = mouse_driver_init,
    .deinit = mouse_driver_deinit,
    .next = NULL
};

// Driver registration
int kernel_driver_register(kernel_driver_t* drv) {
    if (!drv || !drv->name[0]) {
        return -1;
    }
    
    // Check if driver already exists
    kernel_driver_t* existing = kernel_driver_find(drv->name);
    if (existing) {
        return -2; // Driver already registered
    }
    
    // Add to list
    drv->next = driver_list;
    driver_list = drv;
    driver_count++;
    
    drv->status = DRIVER_STATUS_LOADED;
    
    klog("Driver registered");
    
    return 0;
}

int kernel_driver_unregister(kernel_driver_t* drv) {
    if (!drv) return -1;
    
    kernel_driver_t* current = driver_list;
    kernel_driver_t* prev = NULL;
    
    while (current) {
        if (current == drv) {
            if (prev) {
                prev->next = current->next;
            } else {
                driver_list = current->next;
            }
            driver_count--;
            
            klog("Driver unregistered");
            return 0;
        }
        prev = current;
        current = current->next;
    }
    
    return -1; // Driver not found
}

kernel_driver_t* kernel_driver_find(const char* name) {
    kernel_driver_t* current = driver_list;
    
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

kernel_driver_t* kernel_driver_find_by_type(kernel_driver_type_t type) {
    kernel_driver_t* current = driver_list;
    
    while (current) {
        if (current->type == type) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

// Driver initialization
int kernel_drivers_init(void) {
    klog("Initializing kernel drivers...");
    
    // Register all drivers
    kernel_driver_register(&ethernet_driver);
    kernel_driver_register(&wifi_driver);
    kernel_driver_register(&ata_driver);
    kernel_driver_register(&mouse_driver);
    
    // Initialize core subsystems first
    if (pci_drivers_init() != 0) {
        klog("Warning: PCI drivers initialization failed");
    }
    
    if (usb_drivers_init() != 0) {
        klog("Warning: USB drivers initialization failed");
    }
    
    // Initialize hardware drivers
    if (storage_drivers_init() != 0) {
        klog("Warning: Storage drivers initialization failed");
    }
    
    if (network_drivers_init() != 0) {
        klog("Warning: Network drivers initialization failed");
    }
    
    if (input_drivers_init() != 0) {
        klog("Warning: Input drivers initialization failed");
    }
    
    // Initialize audio drivers (NXAudio)
    if (audio_drivers_init() != 0) {
        klog("Warning: Audio drivers initialization failed");
    }
    
    klog("Kernel drivers initialized");
    
    return 0;
}

void kernel_drivers_deinit(void) {
    kernel_driver_t* current = driver_list;
    
    while (current) {
        kernel_driver_t* next = current->next;
        
        if (current->deinit) {
            current->deinit(current);
        }
        
        current = next;
    }
    
    driver_list = NULL;
    driver_count = 0;
    
    klog("All kernel drivers deinitialized");
}

// Enhanced implementations for specific driver types
int network_drivers_init(void) {
    klog("Initializing network drivers...");
    
    // Initialize Ethernet driver
    kernel_driver_t* ethernet = kernel_driver_find("ethernet");
    if (ethernet && ethernet->init) {
        if (ethernet->init(ethernet) == 0) {
            klog("Ethernet driver initialized successfully");
        } else {
            klog("Ethernet driver initialization failed");
        }
    }
    
    // Initialize WiFi driver
    kernel_driver_t* wifi = kernel_driver_find("wifi");
    if (wifi && wifi->init) {
        if (wifi->init(wifi) == 0) {
            klog("WiFi driver initialized successfully");
        } else {
            klog("WiFi driver initialization failed");
        }
    }
    
    return 0;
}

int storage_drivers_init(void) {
    klog("Initializing storage drivers...");
    
    // Initialize ATA driver
    kernel_driver_t* ata = kernel_driver_find("ata");
    if (ata && ata->init) {
        if (ata->init(ata) == 0) {
            klog("ATA driver initialized successfully");
        } else {
            klog("ATA driver initialization failed");
        }
    }
    
    return 0;
}

int input_drivers_init(void) {
    klog("Initializing input drivers...");
    
    // Initialize mouse driver
    kernel_driver_t* mouse = kernel_driver_find("mouse");
    if (mouse && mouse->init) {
        if (mouse->init(mouse) == 0) {
            klog("Mouse driver initialized successfully");
        } else {
            klog("Mouse driver initialization failed");
        }
    }
    
    return 0;
}

int usb_drivers_init(void) {
    klog("USB drivers: Not implemented yet");
    return 0;
}

int pci_drivers_init(void) {
    klog("PCI drivers: Not implemented yet");
    return 0;
}

/* NXAudio Kernel Driver */
extern int nxaudio_kdrv_init(void);
extern void nxaudio_kdrv_debug(void);

int audio_drivers_init(void) {
    klog("Initializing audio drivers (NXAudio)...");
    
    /* Initialize NXAudio kernel driver - detects HDA/AC97 */
    if (nxaudio_kdrv_init() != 0) {
        klog("NXAudio: No audio devices detected");
        return -1;
    }
    
    /* Debug output of detected devices */
    nxaudio_kdrv_debug();
    
    klog("Audio drivers initialized successfully");
    return 0;
} 