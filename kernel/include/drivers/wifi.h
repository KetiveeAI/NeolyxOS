#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stddef.h>
#include "drivers.h"
#include "net/network.h"

/* Note: net_if_type_t, net_state_t, and net_interface_t are defined in net/network.h */

/* ============ WiFi Configuration ============ */

#define WIFI_MAX_SCAN_RESULTS   32
#define WIFI_MAX_SAVED_NETWORKS 16

/* ============ WiFi Vendor/Device IDs ============ */

/* Intel */
#define PCI_VENDOR_INTEL    0x8086
#define INTEL_WIFI_AX200    0x2723
#define INTEL_WIFI_AX201    0x06F0
#define INTEL_WIFI_AX210    0x2725
#define INTEL_WIFI_AX211    0x51F0
#define INTEL_WIFI_AC9260   0x2526
#define INTEL_WIFI_AC9560   0x9DF0
#define INTEL_WIFI_7260     0x08B1
#define INTEL_WIFI_8265     0x24FD

/* Qualcomm/Atheros */
#define PCI_VENDOR_ATHEROS  0x168C
#define PCI_VENDOR_QUALCOMM 0x17CB
#define ATHEROS_AR9285      0x002B
#define ATHEROS_AR9287      0x002E
#define ATHEROS_AR9380      0x0030
#define ATHEROS_QCA6174     0x003E
#define ATHEROS_QCA6390     0x1101

/* Realtek */
#define REALTEK_WIFI_8821   0xC821
#define REALTEK_WIFI_8822   0xC822
#define REALTEK_WIFI_8852   0xC852

/* Broadcom */
#define PCI_VENDOR_BROADCOM 0x14E4
#define BROADCOM_BCM4313    0x4727
#define BROADCOM_BCM43142   0x4365
#define BROADCOM_BCM4360    0x43A0

/* ============ WiFi Standards ============ */

#define WIFI_802_11B    0x01
#define WIFI_802_11G    0x02
#define WIFI_802_11A    0x04
#define WIFI_802_11N    0x08
#define WIFI_802_11AC   0x10
#define WIFI_802_11AX   0x20

/* ============ WiFi Types ============ */

typedef enum {
    WIFI_STATE_DISABLED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

typedef enum {
    WIFI_SEC_OPEN,
    WIFI_SEC_WEP,
    WIFI_SEC_WPA,
    WIFI_SEC_WPA2,
    WIFI_SEC_WPA3
} wifi_security_t;

/* MAC address */
typedef struct {
    uint8_t bytes[6];
} wifi_mac_t;

/* Access point info from scan */
typedef struct {
    char            ssid[33];
    wifi_mac_t      bssid;
    int8_t          rssi;
    wifi_security_t security;
    uint8_t         channel;
} wifi_ap_t;

/* Saved network configuration */
typedef struct {
    char            ssid[33];
    char            password[64];
    wifi_security_t security;
    int             auto_connect;
} wifi_saved_network_t;

/* WiFi device structure */
typedef struct wifi_device {
    char            name[16];
    wifi_state_t    state;
    uint16_t        supported_standards;
    int             supports_5ghz;
    int             supports_6ghz;
    int             max_tx_power;
    volatile void  *mmio_base;
    
    /* Scan results */
    wifi_ap_t       scan_results[WIFI_MAX_SCAN_RESULTS];
    int             scan_count;
    
    /* Connection state */
    wifi_ap_t      *connected_ap;
    int8_t          current_rssi;
    char            password[64];
    
    /* Network interface */
    net_interface_t interface;
    
    /* Linked list */
    struct wifi_device *next;
} wifi_device_t;

/* ============ WiFi API ============ */

int wifi_init(void);
wifi_device_t *wifi_get_device(int index);
wifi_device_t *wifi_get_device_by_name(const char *name);
int wifi_device_count(void);

int wifi_enable(wifi_device_t *dev);
int wifi_disable(wifi_device_t *dev);

int wifi_scan(wifi_device_t *dev);
int wifi_scan_complete(wifi_device_t *dev);
int wifi_get_scan_results(wifi_device_t *dev, wifi_ap_t *results, int max);

int wifi_connect(wifi_device_t *dev, const char *ssid, const char *password);
int wifi_disconnect(wifi_device_t *dev);
wifi_state_t wifi_get_state(wifi_device_t *dev);
wifi_ap_t *wifi_get_connected_ap(wifi_device_t *dev);

int wifi_save_network(const char *ssid, const char *password, wifi_security_t sec);
int wifi_forget_network(const char *ssid);
int wifi_get_saved_networks(wifi_saved_network_t *networks, int max);
int wifi_auto_connect(wifi_device_t *dev);

int wifi_set_tx_power(wifi_device_t *dev, int power_mw);
int wifi_set_channel(wifi_device_t *dev, uint8_t channel);

/* Legacy driver API */
int wifi_driver_init(struct kernel_driver *drv);
int wifi_driver_deinit(struct kernel_driver *drv);

#endif /* WIFI_H */