#include <core/kernel.h>
#include <drivers/ethernet.h>
#include "../../include/drivers/drivers.h"
#include "../serial.h"
#include <utils/log.h>
#include <string.h>
#include <mm/heap.h>
#include <arch/io.h>

/* Memory functions */
extern void *memcpy(void *dest, const void *src, uint64_t n);

// Common Ethernet card I/O ports
#define ETHERNET_COMMAND_PORT 0x00
#define ETHERNET_STATUS_PORT  0x04
#define ETHERNET_DATA_PORT    0x10

// Ethernet frame structure
typedef struct {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
    uint8_t data[1500]; // Max payload
} ethernet_frame_t;

// Ethernet driver data
typedef struct {
    uint16_t io_base;
    uint8_t mac_address[6];
    int is_initialized;
    int is_connected;
    void* rx_buffer;
    void* tx_buffer;
    size_t rx_buffer_size;
    size_t tx_buffer_size;
} ethernet_driver_data_t;

// Ethernet driver instance
static ethernet_driver_data_t ethernet_data = {0};

// Initialize Ethernet hardware
static int ethernet_init_hardware(ethernet_driver_data_t* data) {
    if (!data) return -1;
    
    // Reset the card
    outw(data->io_base + ETHERNET_COMMAND_PORT, 0x0001);
    
    // Wait for reset to complete
    for (int i = 0; i < 1000; i++) {
        if (!(inw(data->io_base + ETHERNET_STATUS_PORT) & 0x0001)) {
            break;
        }
    }
    
    // Read MAC address from EEPROM
    for (int i = 0; i < 6; i++) {
        data->mac_address[i] = inb(data->io_base + 0x20 + i);
    }
    
    // Allocate buffers
    data->rx_buffer_size = 8192;
    data->tx_buffer_size = 8192;
    data->rx_buffer = kmalloc(data->rx_buffer_size);
    data->tx_buffer = kmalloc(data->tx_buffer_size);
    
    if (!data->rx_buffer || !data->tx_buffer) {
        return -1;
    }
    
    // Enable interrupts
    outw(data->io_base + ETHERNET_COMMAND_PORT, 0x0004);
    
    data->is_initialized = 1;
    return 0;
}

// Send Ethernet frame
static int ethernet_send_frame(ethernet_driver_data_t* data, const void* frame, size_t len) {
    if (!data || !data->is_initialized || !frame) return -1;
    
    // Check if transmitter is ready
    if (inw(data->io_base + ETHERNET_STATUS_PORT) & 0x0008) {
        return -1; // Transmitter busy
    }
    
    // Copy frame to TX buffer
    if (len > data->tx_buffer_size) {
        len = data->tx_buffer_size;
    }
    memcpy(data->tx_buffer, frame, len);
    
    // Send frame
    outw(data->io_base + ETHERNET_COMMAND_PORT, 0x0002);
    
    return len;
}

// Receive Ethernet frame
static int ethernet_receive_frame(ethernet_driver_data_t* data, void* buffer, size_t max_len) {
    if (!data || !data->is_initialized || !buffer) return -1;
    
    // Check if data is available
    if (!(inw(data->io_base + ETHERNET_STATUS_PORT) & 0x0001)) {
        return 0; // No data
    }
    
    // Read frame length
    uint16_t frame_len = inw(data->io_base + ETHERNET_DATA_PORT);
    
    if (frame_len > max_len) {
        frame_len = max_len;
    }
    
    // Read frame data
    for (int i = 0; i < frame_len; i++) {
        ((uint8_t*)buffer)[i] = inb(data->io_base + ETHERNET_DATA_PORT);
    }
    
    return frame_len;
}

// Ethernet driver initialization
int ethernet_driver_init(kernel_driver_t* drv) {
    if (!drv) return -1;
    
    // Initialize driver data
    ethernet_data.io_base = 0x2000; // Default I/O base
    ethernet_data.is_initialized = 0;
    ethernet_data.is_connected = 0;
    
    // Initialize hardware
    if (ethernet_init_hardware(&ethernet_data) != 0) {
        klog("Ethernet hardware initialization failed");
        return -1;
    }
    
    klog("Ethernet driver initialized");
    klog("MAC address read successfully");
    
    return 0;
}

// Ethernet driver deinitialization
int ethernet_driver_deinit(kernel_driver_t* drv) {
    if (!drv) return -1;
    
    // Free buffers
    if (ethernet_data.rx_buffer) {
        kfree(ethernet_data.rx_buffer);
        ethernet_data.rx_buffer = NULL;
    }
    
    if (ethernet_data.tx_buffer) {
        kfree(ethernet_data.tx_buffer);
        ethernet_data.tx_buffer = NULL;
    }
    
    ethernet_data.is_initialized = 0;
    klog("Ethernet driver deinitialized");
    
    return 0;
}

// Send packet
int ethernet_send_packet(const void* data, size_t len) {
    ethernet_frame_t frame;
    
    // Set destination MAC (broadcast for now)
    for (int i = 0; i < 6; i++) {
        frame.dest_mac[i] = 0xFF;
    }
    
    // Set source MAC
    memcpy(frame.src_mac, ethernet_data.mac_address, 6);
    
    // Set ethertype (IPv4)
    frame.ethertype = 0x0800;
    
    // Copy data
    if (len > sizeof(frame.data)) {
        len = sizeof(frame.data);
    }
    memcpy(frame.data, data, len);
    
    return ethernet_send_frame(&ethernet_data, &frame, len + 14);
}

// Receive packet
int ethernet_receive_packet(void* buffer, size_t max_len) {
    return ethernet_receive_frame(&ethernet_data, buffer, max_len);
}

// Get MAC address
int ethernet_get_mac_address(uint8_t* mac) {
    if (!mac) return -1;
    memcpy(mac, ethernet_data.mac_address, 6);
    return 0;
}

// Check if driver is ready
int ethernet_is_ready(void) {
    return ethernet_data.is_initialized;
} 