/*
 * NeolyxOS PCI Bus Driver
 * 
 * Production-ready PCI enumeration with:
 * - Configuration space access (I/O and MMIO)
 * - Full bus/device/function scanning
 * - Device class identification
 * - BAR parsing
 * - Driver binding infrastructure
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* ============ PCI Limits ============ */
#define PCI_MAX_BUSES       256
#define PCI_MAX_DEVICES     32
#define PCI_MAX_FUNCTIONS   8
#define PCI_MAX_BARS        6

/* ============ PCI Configuration Registers ============ */
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_PROG_IF         0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS           0x0B
#define PCI_CACHE_LINE_SIZE 0x0C
#define PCI_LATENCY_TIMER   0x0D
#define PCI_HEADER_TYPE     0x0E
#define PCI_BIST            0x0F
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_CARDBUS_CIS     0x28
#define PCI_SUBSYSTEM_VENDOR 0x2C
#define PCI_SUBSYSTEM_ID    0x2E
#define PCI_ROM_ADDRESS     0x30
#define PCI_CAPABILITIES    0x34
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D
#define PCI_MIN_GRANT       0x3E
#define PCI_MAX_LATENCY     0x3F

/* ============ PCI Command Bits ============ */
#define PCI_CMD_IO_SPACE    0x0001
#define PCI_CMD_MEM_SPACE   0x0002
#define PCI_CMD_BUS_MASTER  0x0004
#define PCI_CMD_SPECIAL     0x0008
#define PCI_CMD_MWI         0x0010
#define PCI_CMD_VGA_PALETTE 0x0020
#define PCI_CMD_PARITY      0x0040
#define PCI_CMD_SERR        0x0100
#define PCI_CMD_FAST_BTB    0x0200
#define PCI_CMD_INT_DISABLE 0x0400

/* ============ PCI Header Types ============ */
#define PCI_HEADER_TYPE_NORMAL    0x00
#define PCI_HEADER_TYPE_BRIDGE    0x01
#define PCI_HEADER_TYPE_CARDBUS   0x02
#define PCI_HEADER_TYPE_MULTI     0x80

/* ============ PCI Device Classes ============ */
#define PCI_CLASS_UNCLASSIFIED    0x00
#define PCI_CLASS_STORAGE         0x01
#define PCI_CLASS_NETWORK         0x02
#define PCI_CLASS_DISPLAY         0x03
#define PCI_CLASS_MULTIMEDIA      0x04
#define PCI_CLASS_MEMORY          0x05
#define PCI_CLASS_BRIDGE          0x06
#define PCI_CLASS_COMMUNICATION   0x07
#define PCI_CLASS_SYSTEM          0x08
#define PCI_CLASS_INPUT           0x09
#define PCI_CLASS_DOCKING         0x0A
#define PCI_CLASS_PROCESSOR       0x0B
#define PCI_CLASS_SERIAL          0x0C
#define PCI_CLASS_WIRELESS        0x0D
#define PCI_CLASS_INTELLIGENT     0x0E
#define PCI_CLASS_SATELLITE       0x0F
#define PCI_CLASS_ENCRYPTION      0x10
#define PCI_CLASS_SIGNAL          0x11

/* Storage subclasses */
#define PCI_SUBCLASS_IDE          0x01
#define PCI_SUBCLASS_FLOPPY       0x02
#define PCI_SUBCLASS_IPI          0x03
#define PCI_SUBCLASS_RAID         0x04
#define PCI_SUBCLASS_ATA          0x05
#define PCI_SUBCLASS_SATA         0x06
#define PCI_SUBCLASS_SAS          0x07
#define PCI_SUBCLASS_NVME         0x08

/* Serial bus subclasses */
#define PCI_SUBCLASS_USB          0x03

/* ============ BAR Types ============ */
#define PCI_BAR_IO          0x01
#define PCI_BAR_MEM         0x00
#define PCI_BAR_MEM_32      0x00
#define PCI_BAR_MEM_64      0x04
#define PCI_BAR_PREFETCH    0x08

/* ============ PCI Device Structure ============ */

typedef struct {
    uint32_t base;
    uint32_t size;
    uint8_t  type;      /* IO or Memory */
    uint8_t  memory_type; /* 32-bit or 64-bit */
    uint8_t  prefetchable;
} pci_bar_t;

typedef struct pci_device {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor;
    uint16_t subsystem_id;
    
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    
    uint8_t  header_type;
    uint8_t  interrupt_line;
    uint8_t  interrupt_pin;
    
    pci_bar_t bars[PCI_MAX_BARS];
    int      bar_count;
    
    struct pci_device *next;
} pci_device_t;

/* ============ Public API ============ */

/**
 * pci_init - Initialize PCI subsystem
 * 
 * Enumerates all PCI buses and devices.
 * 
 * Returns: 0 on success, negative on error
 */
int pci_init(void);

/**
 * pci_read_config8/16/32 - Read PCI configuration space
 */
uint8_t  pci_read_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_read_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

/**
 * pci_write_config8/16/32 - Write PCI configuration space
 */
void pci_write_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value);
void pci_write_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value);
void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);

/**
 * pci_find_device - Find device by vendor/device ID
 */
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);

/**
 * pci_find_class - Find device by class/subclass
 */
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);

/**
 * pci_enable_bus_master - Enable bus mastering for device
 */
int pci_enable_bus_master(pci_device_t *dev);

/**
 * pci_enable_memory - Enable memory space access
 */
int pci_enable_memory(pci_device_t *dev);

/**
 * pci_get_device_count - Get number of detected devices
 */
int pci_get_device_count(void);

/**
 * pci_get_device_list - Get head of device list
 */
pci_device_t *pci_get_device_list(void);

/**
 * pci_get_class_name - Get human-readable class name
 */
const char *pci_get_class_name(uint8_t class_code);

#endif /* PCI_H */
