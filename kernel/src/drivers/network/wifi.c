#include <core/kernel.h>
#include "../../include/drivers/drivers.h"
#include "../../include/drivers/wifi_driver.h"
#include "../serial.h"
#include <utils/log.h>
#include <string.h>
#include <mm/heap.h>

/* String functions */
extern void *memset(void *s, int c, uint64_t n);
extern void *memcpy(void *dest, const void *src, uint64_t n);
extern int strcmp(const char *s1, const char *s2);
extern char *strcpy(char *dest, const char *src);

// WiFi hardware registers
#define WIFI_COMMAND_REG    0x00
#define WIFI_STATUS_REG     0x04
#define WIFI_DATA_REG       0x08
#define WIFI_CONFIG_REG     0x0C
#define WIFI_MAC_REG        0x10

// WiFi driver data
static wifi_driver_data_t wifi_data = {0};

// Initialize Intel WiFi hardware
static int intel_wifi_init_hardware(wifi_driver_data_t* data) {
    if (!data) return -1;
    
    // Cast to uint8_t* for proper pointer arithmetic
    volatile uint8_t* regs = (volatile uint8_t*)data->hardware_regs;
    
    // Reset the card
    *(volatile uint32_t*)(regs + WIFI_COMMAND_REG) = 0x00000001;
    
    // Wait for reset to complete
    for (int i = 0; i < 1000; i++) {
        if (!(*(volatile uint32_t*)(regs + WIFI_STATUS_REG) & 0x00000001)) {
            break;
        }
    }
    
    // Read MAC address
    for (int i = 0; i < 6; i++) {
        data->mac_address[i] = *(regs + WIFI_MAC_REG + i);
    }
    
    // Set default mode
    data->mode = WIFI_MODE_STATION;
    
    // Allocate buffers
    data->tx_buffer_size = 4096;
    data->rx_buffer_size = 4096;
    data->tx_buffer = kmalloc(data->tx_buffer_size);
    data->rx_buffer = kmalloc(data->rx_buffer_size);
    
    if (!data->tx_buffer || !data->rx_buffer) {
        return -1;
    }
    
    data->is_initialized = 1;
    return 0;
}

// Initialize Broadcom WiFi hardware
static int broadcom_wifi_init_hardware(wifi_driver_data_t* data) {
    if (!data) return -1;
    
    // Cast to uint8_t* for proper pointer arithmetic
    volatile uint8_t* regs = (volatile uint8_t*)data->hardware_regs;
    
    // Broadcom-specific initialization
    *(volatile uint32_t*)(regs + WIFI_COMMAND_REG) = 0x00000002;
    
    // Read MAC address
    for (int i = 0; i < 6; i++) {
        data->mac_address[i] = *(regs + WIFI_MAC_REG + i);
    }
    
    // Set default mode
    data->mode = WIFI_MODE_STATION;
    
    // Allocate buffers
    data->tx_buffer_size = 4096;
    data->rx_buffer_size = 4096;
    data->tx_buffer = kmalloc(data->tx_buffer_size);
    data->rx_buffer = kmalloc(data->rx_buffer_size);
    
    if (!data->tx_buffer || !data->rx_buffer) {
        return -1;
    }
    
    data->is_initialized = 1;
    return 0;
}

// Initialize Atheros WiFi hardware
static int atheros_wifi_init_hardware(wifi_driver_data_t* data) {
    if (!data) return -1;
    
    // Cast to uint8_t* for proper pointer arithmetic
    volatile uint8_t* regs = (volatile uint8_t*)data->hardware_regs;
    
    // Atheros-specific initialization
    *(volatile uint32_t*)(regs + WIFI_COMMAND_REG) = 0x00000003;
    
    // Read MAC address
    for (int i = 0; i < 6; i++) {
        data->mac_address[i] = *(regs + WIFI_MAC_REG + i);
    }
    
    // Set default mode
    data->mode = WIFI_MODE_STATION;
    
    // Allocate buffers
    data->tx_buffer_size = 4096;
    data->rx_buffer_size = 4096;
    data->tx_buffer = kmalloc(data->tx_buffer_size);
    data->rx_buffer = kmalloc(data->rx_buffer_size);
    
    if (!data->tx_buffer || !data->rx_buffer) {
        return -1;
    }
    
    data->is_initialized = 1;
    return 0;
}

// Initialize Realtek WiFi hardware
static int realtek_wifi_init_hardware(wifi_driver_data_t* data) {
    if (!data) return -1;
    
    // Cast to uint8_t* for proper pointer arithmetic
    volatile uint8_t* regs = (volatile uint8_t*)data->hardware_regs;
    
    // Realtek-specific initialization
    *(volatile uint32_t*)(regs + WIFI_COMMAND_REG) = 0x00000004;
    
    // Read MAC address
    for (int i = 0; i < 6; i++) {
        data->mac_address[i] = *(regs + WIFI_MAC_REG + i);
    }
    
    // Set default mode
    data->mode = WIFI_MODE_STATION;
    
    // Allocate buffers
    data->tx_buffer_size = 4096;
    data->rx_buffer_size = 4096;
    data->tx_buffer = kmalloc(data->tx_buffer_size);
    data->rx_buffer = kmalloc(data->rx_buffer_size);
    
    if (!data->tx_buffer || !data->rx_buffer) {
        return -1;
    }
    
    data->is_initialized = 1;
    return 0;
}

// WiFi driver initialization
int wifi_driver_init(kernel_driver_t* drv) {
    if (!drv) return -1;
    
    // Initialize driver data
    wifi_data.hardware_regs = (void*)0x3000; // Default MMIO base
    wifi_data.chipset = WIFI_CHIPSET_UNKNOWN;
    wifi_data.is_initialized = 0;
    wifi_data.is_connected = 0;
    
    // Cast to uint8_t* for proper pointer arithmetic
    volatile uint8_t* regs = (volatile uint8_t*)wifi_data.hardware_regs;
    
    // Detect chipset (simplified detection)
    uint32_t chip_id = *(volatile uint32_t*)(regs + 0x100);
    
    switch (chip_id & 0xFFFF) {
        case 0x8260:
            wifi_data.chipset = WIFI_CHIPSET_INTEL_8260;
            if (intel_wifi_init_hardware(&wifi_data) != 0) {
                klog("Intel WiFi initialization failed");
                return -1;
            }
            break;
        case 0x4360:
            wifi_data.chipset = WIFI_CHIPSET_BROADCOM_BCM4360;
            if (broadcom_wifi_init_hardware(&wifi_data) != 0) {
                klog("Broadcom WiFi initialization failed");
                return -1;
            }
            break;
        case 0x9280:
            wifi_data.chipset = WIFI_CHIPSET_ATHEROS_AR9280;
            if (atheros_wifi_init_hardware(&wifi_data) != 0) {
                klog("Atheros WiFi initialization failed");
                return -1;
            }
            break;
        case 0x8821:
            wifi_data.chipset = WIFI_CHIPSET_REALTEK_RTL8821CE;
            if (realtek_wifi_init_hardware(&wifi_data) != 0) {
                klog("Realtek WiFi initialization failed");
                return -1;
            }
            break;
        default:
            klog("Unknown WiFi chipset");
            return -1;
    }
    
    klog("WiFi driver initialized successfully");
    return 0;
}

// WiFi driver deinitialization
int wifi_driver_deinit(kernel_driver_t* drv) {
    if (!drv) return -1;
    
    // Free buffers
    if (wifi_data.tx_buffer) {
        kfree(wifi_data.tx_buffer);
        wifi_data.tx_buffer = NULL;
    }
    
    if (wifi_data.rx_buffer) {
        kfree(wifi_data.rx_buffer);
        wifi_data.rx_buffer = NULL;
    }
    
    wifi_data.is_initialized = 0;
    klog("WiFi driver deinitialized");
    
    return 0;
}

// Scan for networks
int wifi_scan_networks(wifi_network_t* networks, int max_count) {
    if (!networks || max_count <= 0 || !wifi_data.is_initialized) return -1;
    
    volatile uint8_t* regs = (volatile uint8_t*)wifi_data.hardware_regs;
    int found = 0;
    
    /* Initiate hardware scan */
    *(volatile uint32_t*)(regs + WIFI_COMMAND_REG) = 0x00000100;  /* SCAN command */
    
    /* Wait for scan completion (poll status register) */
    int timeout = 5000;
    while (timeout-- > 0) {
        uint32_t status = *(volatile uint32_t*)(regs + WIFI_STATUS_REG);
        if (status & 0x00000100) {  /* Scan complete bit */
            break;
        }
        for (volatile int d = 0; d < 1000; d++) {}  /* Small delay */
    }
    
    /* Read scan results from hardware */
    uint32_t result_count = *(volatile uint32_t*)(regs + 0x200);  /* Scan result count */
    if (result_count > max_count) result_count = max_count;
    
    for (uint32_t i = 0; i < result_count && found < max_count; i++) {
        uint32_t offset = 0x204 + (i * 64);  /* Each result is 64 bytes */
        
        /* Read SSID (32 bytes) */
        for (int j = 0; j < 32; j++) {
            networks[found].ssid[j] = *(regs + offset + j);
        }
        networks[found].ssid[31] = '\0';  /* Ensure null-termination */
        
        /* Read signal strength (RSSI) */
        networks[found].signal_strength = (int8_t)*(regs + offset + 32);
        
        /* Read channel */
        networks[found].channel = *(regs + offset + 33);
        
        /* Read security type */
        uint8_t sec = *(regs + offset + 34);
        switch (sec) {
            case 0: networks[found].security = WIFI_SECURITY_OPEN; break;
            case 1: networks[found].security = WIFI_SECURITY_WEP; break;
            case 2: networks[found].security = WIFI_SECURITY_WPA; break;
            case 3: networks[found].security = WIFI_SECURITY_WPA2; break;
            case 4: networks[found].security = WIFI_SECURITY_WPA3; break;
            default: networks[found].security = WIFI_SECURITY_UNKNOWN; break;
        }
        
        found++;
    }
    
    klog("WiFi scan complete");
    return found;
}

// Connect to network
int wifi_connect_network(const char* ssid, const char* password) {
    if (!wifi_data.is_initialized || !ssid) return -1;
    
    volatile uint8_t* regs = (volatile uint8_t*)wifi_data.hardware_regs;
    
    /* Step 1: Write SSID to hardware (max 32 bytes) */
    for (int i = 0; i < 32 && ssid[i]; i++) {
        *(regs + 0x300 + i) = ssid[i];
    }
    
    /* Step 2: Write password/PSK if provided (max 64 bytes for WPA2) */
    if (password) {
        for (int i = 0; i < 64 && password[i]; i++) {
            *(regs + 0x340 + i) = password[i];
        }
        /* Set security mode to WPA2-PSK */
        *(volatile uint32_t*)(regs + 0x380) = 0x03;
    } else {
        /* Open network */
        *(volatile uint32_t*)(regs + 0x380) = 0x00;
    }
    
    /* Step 3: Issue CONNECT command to initiate 802.11 association */
    *(volatile uint32_t*)(regs + WIFI_COMMAND_REG) = 0x00000200;
    
    /* Step 4: Wait for association to complete (poll status) */
    int timeout = 10000;  /* ~10 second timeout */
    while (timeout-- > 0) {
        uint32_t status = *(volatile uint32_t*)(regs + WIFI_STATUS_REG);
        
        if (status & 0x00000200) {  /* Association complete */
            /* Check if successful */
            if (status & 0x00000400) {  /* Error bit */
                klog("WiFi connection failed");
                return -1;
            }
            
            /* Store connected network info */
            for (int i = 0; i < 32 && ssid[i]; i++) {
                wifi_data.current_network.ssid[i] = ssid[i];
            }
            wifi_data.is_connected = 1;
            
            /* Read assigned IP from hardware (DHCP result) */
            wifi_data.current_network.signal_strength = 
                (int8_t)*(regs + 0x390);
            
            klog("WiFi connected successfully");
            return 0;
        }
        
        /* Small delay */
        for (volatile int d = 0; d < 1000; d++) {}
    }
    
    klog("WiFi connection timeout");
    return -1;
}

// Disconnect from network
int wifi_disconnect_network(void) {
    if (!wifi_data.is_initialized) return -1;
    
    if (wifi_data.is_connected) {
        klog("Disconnected from WiFi network");
        wifi_data.is_connected = 0;
        memset(&wifi_data.current_network, 0, sizeof(wifi_network_t));
    }
    
    return 0;
}

// Send WiFi packet
int wifi_send_packet(const void* packet, size_t len) {
    if (!wifi_data.is_initialized || !packet) return -1;
    
    // Cast to uint8_t* for proper pointer arithmetic
    volatile uint8_t* regs = (volatile uint8_t*)wifi_data.hardware_regs;
    
    // Check if transmitter is ready
    if (*(volatile uint32_t*)(regs + WIFI_STATUS_REG) & 0x00000008) {
        return -1; // Transmitter busy
    }
    
    // Copy packet to TX buffer
    if (len > wifi_data.tx_buffer_size) {
        len = wifi_data.tx_buffer_size;
    }
    memcpy(wifi_data.tx_buffer, packet, len);
    
    // Send packet
    *(volatile uint32_t*)(regs + WIFI_COMMAND_REG) = 0x00000010;
    
    return len;
}

// Receive WiFi packet
int wifi_receive_packet(void* buffer, size_t max_len) {
    if (!wifi_data.is_initialized || !buffer) return -1;
    
    // Cast to uint8_t* for proper pointer arithmetic
    volatile uint8_t* regs = (volatile uint8_t*)wifi_data.hardware_regs;
    
    // Check if data is available
    if (!(*(volatile uint32_t*)(regs + WIFI_STATUS_REG) & 0x00000001)) {
        return 0; // No data
    }
    
    // Read packet length
    uint32_t packet_len = *(volatile uint32_t*)(regs + WIFI_DATA_REG);
    
    if (packet_len > max_len) {
        packet_len = max_len;
    }
    
    // Read packet data
    for (uint32_t i = 0; i < packet_len; i++) {
        ((uint8_t*)buffer)[i] = *(regs + WIFI_DATA_REG);
    }
    
    return packet_len;
}

// Get signal strength
int wifi_get_signal_strength(void) {
    if (!wifi_data.is_initialized) return -100;
    
    return wifi_data.current_network.signal_strength;
}

// Set power management
int wifi_set_power_save(wifi_driver_data_t* data, int enable) {
    if (!data || !data->is_initialized) return -1;
    
    // Cast to uint8_t* for proper pointer arithmetic
    volatile uint8_t* regs = (volatile uint8_t*)data->hardware_regs;
    
    uint32_t cmd = enable ? 0x00000020 : 0x00000021;
    *(volatile uint32_t*)(regs + WIFI_COMMAND_REG) = cmd;
    
    klog("WiFi power save changed");
    return 0;
}

// Hardware-specific initialization functions
int intel_wifi_init(wifi_driver_data_t* data) {
    return intel_wifi_init_hardware(data);
}

int broadcom_wifi_init(wifi_driver_data_t* data) {
    return broadcom_wifi_init_hardware(data);
}

int atheros_wifi_init(wifi_driver_data_t* data) {
    return atheros_wifi_init_hardware(data);
}

int realtek_wifi_init(wifi_driver_data_t* data) {
    return realtek_wifi_init_hardware(data);
} 