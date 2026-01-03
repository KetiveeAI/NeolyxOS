#ifndef KERNEL_WIFI_DRIVER_H
#define KERNEL_WIFI_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include "drivers.h"
#include "network.h"

// WiFi chipset types
typedef enum {
    WIFI_CHIPSET_UNKNOWN,
    WIFI_CHIPSET_INTEL_8260,
    WIFI_CHIPSET_INTEL_9260,
    WIFI_CHIPSET_BROADCOM_BCM4360,
    WIFI_CHIPSET_ATHEROS_AR9280,
    WIFI_CHIPSET_REALTEK_RTL8821CE,
    WIFI_CHIPSET_QUALCOMM_QCA9377
} wifi_chipset_t;

// WiFi operation modes
typedef enum {
    WIFI_MODE_STATION,
    WIFI_MODE_AP,
    WIFI_MODE_MONITOR,
    WIFI_MODE_ADHOC
} wifi_mode_t;

// WiFi channel info
typedef struct {
    uint8_t channel;
    uint16_t frequency;
    int signal_strength;
    int noise_level;
} wifi_channel_info_t;

// WiFi packet types
typedef enum {
    WIFI_PACKET_MANAGEMENT,
    WIFI_PACKET_CONTROL,
    WIFI_PACKET_DATA
} wifi_packet_type_t;

// WiFi packet header
typedef struct {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    uint16_t sequence;
} wifi_packet_header_t;

// WiFi standard (802.11 versions)
typedef enum {
    WIFI_STANDARD_UNKNOWN,
    WIFI_STANDARD_B,          // 802.11b (11 Mbps)
    WIFI_STANDARD_G,          // 802.11g (54 Mbps)
    WIFI_STANDARD_N,          // 802.11n WiFi 4 (600 Mbps)
    WIFI_STANDARD_AC,         // 802.11ac WiFi 5 (3.5 Gbps)
    WIFI_STANDARD_AX,         // 802.11ax WiFi 6 (9.6 Gbps)
    WIFI_STANDARD_AX_6E,      // 802.11ax WiFi 6E (6 GHz band)
    WIFI_STANDARD_BE          // 802.11be WiFi 7 (46 Gbps)
} wifi_standard_t;

// WiFi frequency band
typedef enum {
    WIFI_BAND_2_4GHZ = 1,     // 2.4 GHz band
    WIFI_BAND_5GHZ = 2,       // 5 GHz band
    WIFI_BAND_6GHZ = 4        // 6 GHz band (WiFi 6E/7)
} wifi_band_t;

// WiFi driver private data
typedef struct {
    wifi_chipset_t chipset;
    wifi_mode_t mode;
    wifi_standard_t standard;         // WiFi 5/6/7 standard
    uint8_t supported_bands;          // Bitmask of wifi_band_t
    uint8_t mac_address[6];
    int is_initialized;
    int is_connected;
    wifi_network_t current_network;
    wifi_channel_info_t channel_info;
    void* hardware_regs;
    void* tx_buffer;
    void* rx_buffer;
    size_t tx_buffer_size;
    size_t rx_buffer_size;
    
    /* WiFi 5/6/7 Capabilities */
    uint32_t max_link_speed;          // Max speed in Mbps
    uint8_t mu_mimo;                  // Multi-user MIMO support
    uint8_t ofdma;                    // OFDMA support (WiFi 6+)
    uint8_t target_wake_time;         // TWT support (WiFi 6+)
    uint8_t bss_coloring;             // BSS coloring (WiFi 6+)
    uint8_t mlo_support;              // Multi-link operation (WiFi 7)
    uint16_t max_channel_width;       // Max channel width (160/320 MHz)
} wifi_driver_data_t;

// WiFi driver operations
typedef struct {
    int (*init_hardware)(wifi_driver_data_t* data);
    int (*reset_hardware)(wifi_driver_data_t* data);
    int (*set_mode)(wifi_driver_data_t* data, wifi_mode_t mode);
    int (*scan_networks)(wifi_driver_data_t* data, wifi_network_t* networks, int max_count);
    int (*connect_network)(wifi_driver_data_t* data, const char* ssid, const char* password);
    int (*disconnect_network)(wifi_driver_data_t* data);
    int (*send_packet)(wifi_driver_data_t* data, const void* packet, size_t len);
    int (*receive_packet)(wifi_driver_data_t* data, void* buffer, size_t max_len);
    int (*set_channel)(wifi_driver_data_t* data, uint8_t channel);
    int (*get_signal_strength)(wifi_driver_data_t* data);
    int (*power_management)(wifi_driver_data_t* data, int enable);
} wifi_ops_t;

// WiFi driver functions
int wifi_driver_init(kernel_driver_t* drv);
int wifi_driver_deinit(kernel_driver_t* drv);
int wifi_driver_probe(kernel_driver_t* drv);
int wifi_driver_start(kernel_driver_t* drv);
int wifi_driver_stop(kernel_driver_t* drv);

// Hardware-specific functions
int intel_wifi_init(wifi_driver_data_t* data);
int broadcom_wifi_init(wifi_driver_data_t* data);
int atheros_wifi_init(wifi_driver_data_t* data);
int realtek_wifi_init(wifi_driver_data_t* data);

// WiFi utility functions
int wifi_parse_packet(const void* packet, size_t len, wifi_packet_header_t* header);
int wifi_create_packet(void* buffer, size_t max_len, wifi_packet_type_t type, 
                      const uint8_t* dest_addr, const uint8_t* src_addr, 
                      const void* payload, size_t payload_len);
uint16_t wifi_calculate_crc(const void* data, size_t len);

// WiFi power management
int wifi_set_power_save(wifi_driver_data_t* data, int enable);
int wifi_set_tx_power(wifi_driver_data_t* data, int power_level);

// WiFi security functions
int wifi_encrypt_packet(wifi_driver_data_t* data, void* packet, size_t len, const char* key);
int wifi_decrypt_packet(wifi_driver_data_t* data, void* packet, size_t len, const char* key);

#endif // KERNEL_WIFI_DRIVER_H 