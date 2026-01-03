/*
 * NXPCI Kernel Driver (nxpci.kdrv)
 * 
 * NeolyxOS PCI Bus Kernel Driver
 * 
 * Architecture:
 *   [ NXPCI Kernel Driver ] ← Configuration Space Access
 *        ↕ Device Enumeration
 *   [ Device Drivers ] ← Class-specific drivers
 *        ↕ Kernel API
 *   [ User-space ]
 * 
 * Supports: PCI, PCI-X, PCIe
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXPCI_KDRV_H
#define NXPCI_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXPCI_KDRV_VERSION    "1.0.0"
#define NXPCI_KDRV_ABI        1

/* ============ PCI Class Codes ============ */

typedef enum {
    NXPCI_CLASS_UNCLASSIFIED = 0x00,
    NXPCI_CLASS_STORAGE = 0x01,
    NXPCI_CLASS_NETWORK = 0x02,
    NXPCI_CLASS_DISPLAY = 0x03,
    NXPCI_CLASS_MULTIMEDIA = 0x04,
    NXPCI_CLASS_MEMORY = 0x05,
    NXPCI_CLASS_BRIDGE = 0x06,
    NXPCI_CLASS_COMMUNICATION = 0x07,
    NXPCI_CLASS_SYSTEM = 0x08,
    NXPCI_CLASS_INPUT = 0x09,
    NXPCI_CLASS_DOCKING = 0x0A,
    NXPCI_CLASS_PROCESSOR = 0x0B,
    NXPCI_CLASS_SERIAL = 0x0C,
    NXPCI_CLASS_WIRELESS = 0x0D,
} nxpci_class_t;

/* ============ BAR Types ============ */

typedef enum {
    NXPCI_BAR_NONE = 0,
    NXPCI_BAR_IO,
    NXPCI_BAR_MEM32,
    NXPCI_BAR_MEM64,
} nxpci_bar_type_t;

/* ============ BAR Info ============ */

typedef struct {
    nxpci_bar_type_t type;
    uint64_t         base;
    uint64_t         size;
    uint8_t          prefetchable;
} nxpci_bar_info_t;

/* ============ Device Info ============ */

#define NXPCI_MAX_BARS    6

typedef struct {
    /* Location */
    uint8_t         bus;
    uint8_t         device;
    uint8_t         function;
    
    /* Identification */
    uint16_t        vendor_id;
    uint16_t        device_id;
    uint16_t        subsystem_vendor;
    uint16_t        subsystem_id;
    
    /* Classification */
    uint8_t         class_code;
    uint8_t         subclass;
    uint8_t         prog_if;
    uint8_t         revision;
    
    /* Interrupt */
    uint8_t         irq_line;
    uint8_t         irq_pin;
    
    /* BARs */
    uint8_t         bar_count;
    nxpci_bar_info_t bars[NXPCI_MAX_BARS];
    
    /* Flags */
    uint8_t         is_bridge;
    uint8_t         is_multifunction;
} nxpci_device_info_t;

/* ============ Kernel Driver API ============ */

/**
 * Initialize PCI subsystem
 */
int nxpci_kdrv_init(void);

/**
 * Shutdown PCI subsystem
 */
void nxpci_kdrv_shutdown(void);

/**
 * Get number of PCI devices
 */
int nxpci_kdrv_device_count(void);

/**
 * Get device info by index
 */
int nxpci_kdrv_device_info(int index, nxpci_device_info_t *info);

/**
 * Find device by vendor/device ID
 */
int nxpci_kdrv_find_device(uint16_t vendor_id, uint16_t device_id);

/**
 * Find device by class
 */
int nxpci_kdrv_find_class(uint8_t class_code, uint8_t subclass);

/**
 * Enable bus mastering for device
 */
int nxpci_kdrv_enable_busmaster(int device_index);

/**
 * Enable memory space for device
 */
int nxpci_kdrv_enable_memory(int device_index);

/**
 * Get class name
 */
const char *nxpci_kdrv_class_name(uint8_t class_code);

/**
 * Read configuration space
 */
uint32_t nxpci_kdrv_config_read32(int device_index, uint8_t offset);
uint16_t nxpci_kdrv_config_read16(int device_index, uint8_t offset);
uint8_t  nxpci_kdrv_config_read8(int device_index, uint8_t offset);

/**
 * Write configuration space
 */
void nxpci_kdrv_config_write32(int device_index, uint8_t offset, uint32_t value);
void nxpci_kdrv_config_write16(int device_index, uint8_t offset, uint16_t value);
void nxpci_kdrv_config_write8(int device_index, uint8_t offset, uint8_t value);

/**
 * Debug output
 */
void nxpci_kdrv_debug(void);

#endif /* NXPCI_KDRV_H */
