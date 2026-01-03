#ifndef KERNEL_DRIVERS_H
#define KERNEL_DRIVERS_H

#include <stdint.h>
#include <stddef.h>

// Driver types for kernel
typedef enum {
    DRIVER_NETWORK_WIFI,
    DRIVER_NETWORK_ETHERNET,
    DRIVER_STORAGE_DISK,
    DRIVER_STORAGE_USB,
    DRIVER_INPUT_KEYBOARD,
    DRIVER_INPUT_MOUSE,
    DRIVER_VIDEO_GRAPHICS,
    DRIVER_AUDIO,
    DRIVER_USB_CONTROLLER,
    DRIVER_PCI_CONTROLLER,
    DRIVER_SERIAL_UART,
    DRIVER_TIMER,
    DRIVER_RTC,
    DRIVER_ACPI
} kernel_driver_type_t;

// Driver status
typedef enum {
    DRIVER_STATUS_UNLOADED,
    DRIVER_STATUS_LOADED,
    DRIVER_STATUS_INITIALIZED,
    DRIVER_STATUS_RUNNING,
    DRIVER_STATUS_ERROR,
    DRIVER_STATUS_SUSPENDED
} kernel_driver_status_t;

// Base driver structure
typedef struct kernel_driver {
    char name[64];
    char version[16];
    kernel_driver_type_t type;
    kernel_driver_status_t status;
    uint32_t device_id;
    uint32_t vendor_id;
    
    // Function pointers
    int (*init)(struct kernel_driver* drv);
    int (*deinit)(struct kernel_driver* drv);
    int (*probe)(struct kernel_driver* drv);
    int (*start)(struct kernel_driver* drv);
    int (*stop)(struct kernel_driver* drv);
    int (*suspend)(struct kernel_driver* drv);
    int (*resume)(struct kernel_driver* drv);
    
    // Driver-specific data
    void* private_data;
    
    // Next driver in list
    struct kernel_driver* next;
} kernel_driver_t;

// Network driver specific structures
typedef struct {
    uint8_t mac_address[6];
    uint32_t ip_address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t mtu;
    int is_wifi;
    char ssid[32];
    char password[64];
} network_config_t;

typedef struct {
    int (*send_packet)(void* driver, void* data, size_t len);
    int (*receive_packet)(void* driver, void* buffer, size_t max_len);
    int (*get_mac)(void* driver, uint8_t* mac);
    int (*set_ip)(void* driver, uint32_t ip, uint32_t netmask, uint32_t gateway);
    int (*connect_wifi)(void* driver, const char* ssid, const char* password);
    int (*disconnect)(void* driver);
} network_ops_t;

// Storage driver specific structures
typedef struct {
    uint64_t total_sectors;
    uint32_t sector_size;
    uint32_t max_transfer_size;
    int is_removable;
    int is_readonly;
} storage_info_t;

typedef struct {
    int (*read_sectors)(void* driver, uint64_t lba, uint32_t count, void* buffer);
    int (*write_sectors)(void* driver, uint64_t lba, uint32_t count, const void* buffer);
    int (*get_info)(void* driver, storage_info_t* info);
    int (*flush_cache)(void* driver);
} storage_ops_t;

// Input driver specific structures
typedef struct {
    uint8_t keycode;
    uint8_t modifiers;
    int is_pressed;
} keyboard_event_t;

typedef struct {
    int delta_x;
    int delta_y;
    int buttons;
    int scroll;
} mouse_event_t;

typedef struct {
    int (*read_keyboard)(void* driver, keyboard_event_t* event);
    int (*read_mouse)(void* driver, mouse_event_t* event);
    int (*set_leds)(void* driver, uint8_t leds);
} input_ops_t;

// Driver management functions
int kernel_driver_register(kernel_driver_t* drv);
int kernel_driver_unregister(kernel_driver_t* drv);
kernel_driver_t* kernel_driver_find(const char* name);
kernel_driver_t* kernel_driver_find_by_type(kernel_driver_type_t type);

// Driver initialization
int kernel_drivers_init(void);
void kernel_drivers_deinit(void);

// Specific driver initialization
int network_drivers_init(void);
int storage_drivers_init(void);
int input_drivers_init(void);
int usb_drivers_init(void);
int pci_drivers_init(void);

#endif // KERNEL_DRIVERS_H 