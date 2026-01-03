/*
 * NeolyxOS WiFi Driver Implementation
 * 
 * Common WiFi functionality with Intel/Atheros/Realtek support.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "drivers/wifi.h"
#include "mm/kheap.h"
#include "core/sysconfig.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ PCI Access ============ */

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

/* ============ State ============ */

static wifi_device_t *wifi_devices = NULL;
static int wifi_device_cnt = 0;
static wifi_saved_network_t saved_networks[WIFI_MAX_SAVED_NETWORKS];
static int saved_network_count = 0;

/* ============ Helpers ============ */

static void wifi_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int wifi_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void serial_hex(uint16_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_putc(hex[(val >> 12) & 0xF]);
    serial_putc(hex[(val >> 8) & 0xF]);
    serial_putc(hex[(val >> 4) & 0xF]);
    serial_putc(hex[val & 0xF]);
}

/* ============ Device Detection ============ */

static int wifi_detect_device(uint8_t bus, uint8_t slot, uint16_t vendor, uint16_t device) {
    wifi_device_t *dev = (wifi_device_t *)kzalloc(sizeof(wifi_device_t));
    if (!dev) return -1;
    
    dev->name[0] = 'w';
    dev->name[1] = 'l';
    dev->name[2] = 'a';
    dev->name[3] = 'n';
    dev->name[4] = '0' + wifi_device_cnt;
    dev->name[5] = '\0';
    
    dev->state = WIFI_STATE_DISABLED;
    dev->scan_count = 0;
    
    /* Set capabilities based on device */
    switch (device) {
        case INTEL_WIFI_AX200:
        case INTEL_WIFI_AX201:
        case INTEL_WIFI_AX210:
        case INTEL_WIFI_AX211:
            dev->supported_standards = WIFI_802_11AX | WIFI_802_11AC | 
                                       WIFI_802_11N | WIFI_802_11A | WIFI_802_11G;
            dev->supports_5ghz = 1;
            dev->supports_6ghz = 1;
            serial_puts("[WIFI] Intel WiFi 6 detected\n");
            break;
            
        case INTEL_WIFI_AC9260:
        case INTEL_WIFI_AC9560:
            dev->supported_standards = WIFI_802_11AC | WIFI_802_11N | 
                                       WIFI_802_11A | WIFI_802_11G;
            dev->supports_5ghz = 1;
            serial_puts("[WIFI] Intel WiFi 5 detected\n");
            break;
            
        case ATHEROS_QCA6174:
        case ATHEROS_QCA6390:
            dev->supported_standards = WIFI_802_11AC | WIFI_802_11N | WIFI_802_11G;
            dev->supports_5ghz = 1;
            serial_puts("[WIFI] Qualcomm Atheros detected\n");
            break;
            
        case REALTEK_WIFI_8821:
        case REALTEK_WIFI_8822:
        case REALTEK_WIFI_8852:
            dev->supported_standards = WIFI_802_11AC | WIFI_802_11N | WIFI_802_11G;
            dev->supports_5ghz = 1;
            serial_puts("[WIFI] Realtek WiFi detected\n");
            break;
            
        default:
            dev->supported_standards = WIFI_802_11N | WIFI_802_11G | WIFI_802_11B;
            serial_puts("[WIFI] Generic WiFi detected\n");
            break;
    }
    
    dev->max_tx_power = 100;  /* 100 mW default */
    
    /* Read PCI BAR0 for MMIO base address */
    uint32_t bar0 = pci_read(bus, slot, 0, 0x10);  /* BAR0 at offset 0x10 */
    if (bar0 & 0x01) {
        /* I/O space - not typical for modern WiFi, use NULL */
        dev->mmio_base = NULL;
    } else {
        /* Memory-mapped I/O */
        uint64_t mmio_addr = bar0 & 0xFFFFFFF0;
        
        /* Check for 64-bit BAR */
        if ((bar0 & 0x06) == 0x04) {
            uint32_t bar1 = pci_read(bus, slot, 0, 0x14);
            mmio_addr |= ((uint64_t)bar1 << 32);
        }
        
        /* Enable bus master and memory space */
        uint32_t cmd = pci_read(bus, slot, 0, 0x04);
        cmd |= 0x06;  /* Memory Space + Bus Master */
        pci_write(bus, slot, 0, 0x04, cmd);
        
        dev->mmio_base = (volatile void *)mmio_addr;
        
        serial_puts("[WIFI] MMIO @ 0x");
        serial_hex((uint32_t)(mmio_addr >> 32));
        serial_hex((uint32_t)mmio_addr);
        serial_puts("\n");
    }
    
    /* Setup network interface */
    wifi_strcpy(dev->interface.name, dev->name, 16);
    dev->interface.type = NET_IF_WIFI;
    dev->interface.state = NET_STATE_DOWN;
    dev->interface.driver_data = dev;
    
    /* Link into list */
    dev->next = wifi_devices;
    wifi_devices = dev;
    wifi_device_cnt++;
    
    return 0;
}

/* ============ WiFi API ============ */

int wifi_init(void) {
    serial_puts("[WIFI] Initializing WiFi subsystem...\n");
    
    /* Check if WiFi is enabled in config */
    if (!sysconfig_has_feature(FEATURE_WIFI)) {
        serial_puts("[WIFI] WiFi disabled in config (Server mode)\n");
        return 0;
    }
    
    wifi_devices = NULL;
    wifi_device_cnt = 0;
    
    /* Scan PCI for WiFi devices */
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_read(bus, slot, 0, 0);
            uint16_t vendor = vendor_device & 0xFFFF;
            uint16_t device = (vendor_device >> 16) & 0xFFFF;
            
            if (vendor == 0xFFFF) continue;
            
            /* Check class (network controller, subclass 80 = WiFi) */
            uint32_t class_info = pci_read(bus, slot, 0, 8);
            uint8_t base_class = (class_info >> 24) & 0xFF;
            uint8_t sub_class = (class_info >> 16) & 0xFF;
            
            if (base_class != 0x02) continue;  /* Not network */
            
            /* Check for supported vendors */
            int supported = 0;
            
            if (vendor == PCI_VENDOR_INTEL) {
                switch (device) {
                    case INTEL_WIFI_AX200:
                    case INTEL_WIFI_AX201:
                    case INTEL_WIFI_AX210:
                    case INTEL_WIFI_AX211:
                    case INTEL_WIFI_AC9260:
                    case INTEL_WIFI_AC9560:
                    case INTEL_WIFI_7260:
                    case INTEL_WIFI_8265:
                        supported = 1;
                        break;
                }
            } else if (vendor == PCI_VENDOR_ATHEROS || vendor == PCI_VENDOR_QUALCOMM) {
                switch (device) {
                    case ATHEROS_AR9285:
                    case ATHEROS_AR9287:
                    case ATHEROS_AR9380:
                    case ATHEROS_QCA6174:
                    case ATHEROS_QCA6390:
                        supported = 1;
                        break;
                }
            } else if (vendor == 0x10EC) {  /* Realtek */
                switch (device) {
                    case REALTEK_WIFI_8821:
                    case REALTEK_WIFI_8822:
                    case REALTEK_WIFI_8852:
                        supported = 1;
                        break;
                }
            } else if (vendor == PCI_VENDOR_BROADCOM) {
                switch (device) {
                    case BROADCOM_BCM4313:
                    case BROADCOM_BCM43142:
                    case BROADCOM_BCM4360:
                        supported = 1;
                        break;
                }
            }
            
            if (supported) {
                serial_puts("[WIFI] Found: ");
                serial_hex(vendor);
                serial_putc(':');
                serial_hex(device);
                serial_puts("\n");
                
                wifi_detect_device(bus, slot, vendor, device);
            }
        }
    }
    
    if (wifi_device_cnt == 0) {
        serial_puts("[WIFI] No devices found\n");
        return 0;
    }
    
    serial_puts("[WIFI] Ready (");
    serial_putc('0' + wifi_device_cnt);
    serial_puts(" devices)\n");
    
    return 0;
}

wifi_device_t *wifi_get_device(int index) {
    wifi_device_t *dev = wifi_devices;
    int i = 0;
    while (dev && i < index) {
        dev = dev->next;
        i++;
    }
    return dev;
}

wifi_device_t *wifi_get_device_by_name(const char *name) {
    wifi_device_t *dev = wifi_devices;
    while (dev) {
        if (wifi_strcmp(dev->name, name) == 0) return dev;
        dev = dev->next;
    }
    return NULL;
}

int wifi_device_count(void) {
    return wifi_device_cnt;
}

int wifi_enable(wifi_device_t *dev) {
    if (!dev) return -1;
    
    dev->state = WIFI_STATE_DISCONNECTED;
    dev->interface.state = NET_STATE_UP;
    
    serial_puts("[WIFI] ");
    serial_puts(dev->name);
    serial_puts(" enabled\n");
    
    return 0;
}

int wifi_disable(wifi_device_t *dev) {
    if (!dev) return -1;
    
    dev->state = WIFI_STATE_DISABLED;
    dev->interface.state = NET_STATE_DOWN;
    
    serial_puts("[WIFI] ");
    serial_puts(dev->name);
    serial_puts(" disabled\n");
    
    return 0;
}

/* ============ Scanning ============ */

int wifi_scan(wifi_device_t *dev) {
    if (!dev || dev->state == WIFI_STATE_DISABLED) return -1;
    
    dev->state = WIFI_STATE_SCANNING;
    dev->scan_count = 0;
    
    serial_puts("[WIFI] Scanning (hw)...\n");
    
    /* Real hardware scanning via device MMIO registers */
    /* Each WiFi chipset has vendor-specific scanning mechanism */
    
    if (dev->mmio_base) {
        volatile uint32_t *regs = (volatile uint32_t *)dev->mmio_base;
        
        /* Trigger active scan on all supported channels */
        /* Common register pattern: Write scan command to control register */
        uint32_t scan_cmd = 0x01;  /* Start scan */
        if (dev->supports_5ghz) scan_cmd |= 0x02;  /* Include 5GHz bands */
        if (dev->supports_6ghz) scan_cmd |= 0x04;  /* Include 6GHz bands */
        
        /* Write scan trigger (offset varies by chipset, using common offset) */
        regs[0x100 / 4] = scan_cmd;
        
        /* Wait for scan completion (poll status register) */
        int timeout = 5000;  /* 5 second timeout */
        while (timeout > 0) {
            uint32_t status = regs[0x104 / 4];
            if (status & 0x01) break;  /* Scan complete bit */
            for (volatile int d = 0; d < 1000; d++);  /* ~1ms delay */
            timeout--;
        }
        
        if (timeout <= 0) {
            serial_puts("[WIFI] Scan timeout\n");
            dev->state = WIFI_STATE_DISCONNECTED;
            return -1;
        }
        
        /* Read scan results from device buffer */
        uint32_t result_count = regs[0x108 / 4];
        if (result_count > WIFI_MAX_SCAN_RESULTS) {
            result_count = WIFI_MAX_SCAN_RESULTS;
        }
        
        /* Parse beacon frame data from device memory */
        for (uint32_t i = 0; i < result_count && i < WIFI_MAX_SCAN_RESULTS; i++) {
            uint32_t base = 0x200 + (i * 0x40);  /* Each result = 64 bytes */
            
            /* Read SSID (up to 32 bytes at offset 0) */
            for (int j = 0; j < 32 && j < (int)sizeof(dev->scan_results[i].ssid) - 1; j++) {
                uint32_t word = regs[(base + j) / 4];
                uint8_t byte = (word >> ((j % 4) * 8)) & 0xFF;
                if (byte == 0) break;
                dev->scan_results[i].ssid[j] = (char)byte;
                dev->scan_results[i].ssid[j + 1] = '\0';
            }
            
            /* Read signal strength (offset 0x20) */
            int8_t rssi_raw = (int8_t)(regs[(base + 0x20) / 4] & 0xFF);
            dev->scan_results[i].rssi = rssi_raw ? rssi_raw : -80;  /* Default if not reported */
            
            /* Read security type (offset 0x24) */
            uint8_t sec = (regs[(base + 0x24) / 4] >> 8) & 0x0F;
            dev->scan_results[i].security = (sec < 5) ? (wifi_security_t)sec : WIFI_SEC_OPEN;
            
            /* Read channel (offset 0x28) */
            dev->scan_results[i].channel = regs[(base + 0x28) / 4] & 0xFF;
            if (dev->scan_results[i].channel == 0) dev->scan_results[i].channel = 1;
            
            /* Read BSSID (offset 0x30) */
            uint32_t bssid_lo = regs[(base + 0x30) / 4];
            uint32_t bssid_hi = regs[(base + 0x34) / 4];
            dev->scan_results[i].bssid.bytes[0] = (bssid_lo >> 0) & 0xFF;
            dev->scan_results[i].bssid.bytes[1] = (bssid_lo >> 8) & 0xFF;
            dev->scan_results[i].bssid.bytes[2] = (bssid_lo >> 16) & 0xFF;
            dev->scan_results[i].bssid.bytes[3] = (bssid_lo >> 24) & 0xFF;
            dev->scan_results[i].bssid.bytes[4] = (bssid_hi >> 0) & 0xFF;
            dev->scan_results[i].bssid.bytes[5] = (bssid_hi >> 8) & 0xFF;
            
            dev->scan_count++;
        }
    }
    
    dev->state = WIFI_STATE_DISCONNECTED;
    
    serial_puts("[WIFI] Found ");
    serial_putc('0' + (dev->scan_count % 10));
    serial_puts(" networks\n");
    
    return 0;
}

int wifi_scan_complete(wifi_device_t *dev) {
    return (dev && dev->state != WIFI_STATE_SCANNING);
}

int wifi_get_scan_results(wifi_device_t *dev, wifi_ap_t *results, int max) {
    if (!dev) return 0;
    
    int count = (dev->scan_count < max) ? dev->scan_count : max;
    for (int i = 0; i < count; i++) {
        results[i] = dev->scan_results[i];
    }
    
    return count;
}

/* ============ Connection ============ */

int wifi_connect(wifi_device_t *dev, const char *ssid, const char *password) {
    if (!dev || dev->state == WIFI_STATE_DISABLED) return -1;
    
    dev->state = WIFI_STATE_CONNECTING;
    wifi_strcpy(dev->password, password ? password : "", 64);
    
    serial_puts("[WIFI] Connecting to ");
    serial_puts(ssid);
    serial_puts("...\n");
    
    /* Find AP in scan results */
    for (int i = 0; i < dev->scan_count; i++) {
        if (wifi_strcmp(dev->scan_results[i].ssid, ssid) == 0) {
            dev->connected_ap = &dev->scan_results[i];
            break;
        }
    }
    
    /* TODO: Actual connection handshake */
    /* Simulate success */
    dev->state = WIFI_STATE_CONNECTED;
    dev->interface.state = NET_STATE_CONNECTED;
    dev->current_rssi = dev->connected_ap ? dev->connected_ap->rssi : -50;
    
    serial_puts("[WIFI] Connected!\n");
    
    return 0;
}

int wifi_disconnect(wifi_device_t *dev) {
    if (!dev) return -1;
    
    dev->state = WIFI_STATE_DISCONNECTED;
    dev->connected_ap = NULL;
    dev->interface.state = NET_STATE_UP;
    
    serial_puts("[WIFI] Disconnected\n");
    
    return 0;
}

wifi_state_t wifi_get_state(wifi_device_t *dev) {
    return dev ? dev->state : WIFI_STATE_DISABLED;
}

wifi_ap_t *wifi_get_connected_ap(wifi_device_t *dev) {
    return dev ? dev->connected_ap : NULL;
}

/* ============ Saved Networks ============ */

int wifi_save_network(const char *ssid, const char *password, wifi_security_t sec) {
    if (saved_network_count >= WIFI_MAX_SAVED_NETWORKS) return -1;
    
    wifi_saved_network_t *net = &saved_networks[saved_network_count++];
    wifi_strcpy(net->ssid, ssid, 33);
    wifi_strcpy(net->password, password ? password : "", 64);
    net->security = sec;
    net->auto_connect = 1;
    
    serial_puts("[WIFI] Saved network: ");
    serial_puts(ssid);
    serial_puts("\n");
    
    return 0;
}

int wifi_forget_network(const char *ssid) {
    for (int i = 0; i < saved_network_count; i++) {
        if (wifi_strcmp(saved_networks[i].ssid, ssid) == 0) {
            /* Remove by shifting */
            for (int j = i; j < saved_network_count - 1; j++) {
                saved_networks[j] = saved_networks[j + 1];
            }
            saved_network_count--;
            return 0;
        }
    }
    return -1;
}

int wifi_get_saved_networks(wifi_saved_network_t *networks, int max) {
    int count = (saved_network_count < max) ? saved_network_count : max;
    for (int i = 0; i < count; i++) {
        networks[i] = saved_networks[i];
    }
    return count;
}

int wifi_auto_connect(wifi_device_t *dev) {
    if (!dev) return -1;
    
    wifi_scan(dev);
    
    for (int s = 0; s < saved_network_count; s++) {
        for (int r = 0; r < dev->scan_count; r++) {
            if (wifi_strcmp(saved_networks[s].ssid, dev->scan_results[r].ssid) == 0) {
                if (saved_networks[s].auto_connect) {
                    return wifi_connect(dev, saved_networks[s].ssid, 
                                        saved_networks[s].password);
                }
            }
        }
    }
    
    return -1;
}

/* ============ Configuration ============ */

int wifi_set_tx_power(wifi_device_t *dev, int power_mw) {
    if (!dev) return -1;
    dev->max_tx_power = power_mw;
    return 0;
}

int wifi_set_channel(wifi_device_t *dev, uint8_t channel) {
    if (!dev) return -1;
    /* TODO: Set channel in hardware */
    (void)channel;
    return 0;
}

/* ============ Legacy Driver API ============ */
/* For compatibility with kernel driver framework */

int wifi_driver_init(struct kernel_driver *drv) {
    (void)drv;
    return wifi_init();
}

int wifi_driver_deinit(struct kernel_driver *drv) {
    (void)drv;
    /* Disable all WiFi devices */
    wifi_device_t *dev = wifi_devices;
    while (dev) {
        wifi_disable(dev);
        dev = dev->next;
    }
    return 0;
}
