/*
 * NeolyxOS AHCI/SATA Storage Driver
 * 
 * AHCI driver for SATA disk access.
 * Supports read/write of disk sectors.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_AHCI_H
#define NEOLYX_AHCI_H

#include <stdint.h>
#include <stddef.h>

/* ============ AHCI Constants ============ */

#define AHCI_MAX_PORTS      32
#define AHCI_SECTOR_SIZE    512

/* AHCI Port Signatures */
#define SATA_SIG_ATA        0x00000101  /* SATA drive */
#define SATA_SIG_ATAPI      0xEB140101  /* SATAPI drive */
#define SATA_SIG_SEMB       0xC33C0101  /* Enclosure management bridge */
#define SATA_SIG_PM         0x96690101  /* Port multiplier */

/* Port Command Bits */
#define HBA_PORT_CMD_ST     0x0001  /* Start */
#define HBA_PORT_CMD_FRE    0x0010  /* FIS Receive Enable */
#define HBA_PORT_CMD_FR     0x4000  /* FIS Receive Running */
#define HBA_PORT_CMD_CR     0x8000  /* Command List Running */

/* FIS Types */
#define FIS_TYPE_REG_H2D    0x27    /* Register FIS - Host to Device */
#define FIS_TYPE_REG_D2H    0x34    /* Register FIS - Device to Host */
#define FIS_TYPE_DMA_ACT    0x39    /* DMA Activate FIS */
#define FIS_TYPE_DMA_SETUP  0x41    /* DMA Setup FIS */
#define FIS_TYPE_DATA       0x46    /* Data FIS */
#define FIS_TYPE_BIST       0x58    /* BIST Activate FIS */
#define FIS_TYPE_PIO_SETUP  0x5F    /* PIO Setup FIS */

/* ATA Commands */
#define ATA_CMD_READ_DMA_EX 0x25    /* Read DMA Extended (48-bit LBA) */
#define ATA_CMD_WRITE_DMA_EX 0x35   /* Write DMA Extended */
#define ATA_CMD_IDENTIFY    0xEC    /* Identify Device */
#define ATA_CMD_FLUSH_CACHE 0xE7    /* Flush Cache (28-bit) */
#define ATA_CMD_FLUSH_CACHE_EX 0xEA /* Flush Cache Extended (48-bit) */

/* ============ AHCI Structures ============ */

/* HBA Memory Registers */
typedef volatile struct {
    uint32_t cap;           /* Host Capabilities */
    uint32_t ghc;           /* Global Host Control */
    uint32_t is;            /* Interrupt Status */
    uint32_t pi;            /* Ports Implemented */
    uint32_t vs;            /* Version */
    uint32_t ccc_ctl;       /* Command Completion Coalescing Control */
    uint32_t ccc_ports;     /* Command Completion Coalescing Ports */
    uint32_t em_loc;        /* Enclosure Management Location */
    uint32_t em_ctl;        /* Enclosure Management Control */
    uint32_t cap2;          /* Host Capabilities Extended */
    uint32_t bohc;          /* BIOS/OS Handoff Control */
    uint8_t  reserved[0xA0 - 0x2C];
    uint8_t  vendor[0x100 - 0xA0];
} hba_mem_t;

/* HBA Port Registers */
typedef volatile struct {
    uint32_t clb;           /* Command List Base Address (low) */
    uint32_t clbu;          /* Command List Base Address (high) */
    uint32_t fb;            /* FIS Base Address (low) */
    uint32_t fbu;           /* FIS Base Address (high) */
    uint32_t is;            /* Interrupt Status */
    uint32_t ie;            /* Interrupt Enable */
    uint32_t cmd;           /* Command and Status */
    uint32_t reserved0;
    uint32_t tfd;           /* Task File Data */
    uint32_t sig;           /* Signature */
    uint32_t ssts;          /* SATA Status */
    uint32_t sctl;          /* SATA Control */
    uint32_t serr;          /* SATA Error */
    uint32_t sact;          /* SATA Active */
    uint32_t ci;            /* Command Issue */
    uint32_t sntf;          /* SATA Notification */
    uint32_t fbs;           /* FIS-based Switching Control */
    uint32_t reserved1[11];
    uint32_t vendor[4];
} hba_port_t;

/* Command Header */
typedef struct {
    uint8_t  cfl:5;         /* Command FIS Length (in DWORDs) */
    uint8_t  atapi:1;       /* ATAPI */
    uint8_t  write:1;       /* Write (1: H2D, 0: D2H) */
    uint8_t  prefetch:1;    /* Prefetchable */
    
    uint8_t  reset:1;       /* Reset */
    uint8_t  bist:1;        /* BIST */
    uint8_t  clear:1;       /* Clear Busy upon R_OK */
    uint8_t  reserved0:1;
    uint8_t  pmp:4;         /* Port Multiplier Port */
    
    uint16_t prdtl;         /* Physical Region Descriptor Table Length */
    
    volatile uint32_t prdbc; /* PRD Byte Count */
    
    uint32_t ctba;          /* Command Table Base Address (low) */
    uint32_t ctbau;         /* Command Table Base Address (high) */
    
    uint32_t reserved1[4];
} __attribute__((packed)) hba_cmd_header_t;

/* Physical Region Descriptor Table Entry */
typedef struct {
    uint32_t dba;           /* Data Base Address (low) */
    uint32_t dbau;          /* Data Base Address (high) */
    uint32_t reserved0;
    
    uint32_t dbc:22;        /* Byte Count (0-based) */
    uint32_t reserved1:9;
    uint32_t interrupt:1;   /* Interrupt on Completion */
} __attribute__((packed)) hba_prdt_entry_t;

/* Command Table */
typedef struct {
    uint8_t cfis[64];       /* Command FIS */
    uint8_t acmd[16];       /* ATAPI Command */
    uint8_t reserved[48];
    hba_prdt_entry_t prdt[8]; /* Physical Region Descriptor Table */
} __attribute__((packed)) hba_cmd_tbl_t;

/* FIS Register H2D */
typedef struct {
    uint8_t fis_type;       /* FIS_TYPE_REG_H2D */
    uint8_t pmport:4;       /* Port Multiplier */
    uint8_t reserved0:3;
    uint8_t c:1;            /* Command (1) / Control (0) */
    uint8_t command;        /* Command register */
    uint8_t feature_low;    /* Feature register low */
    
    uint8_t lba0;           /* LBA low byte */
    uint8_t lba1;           /* LBA mid byte */
    uint8_t lba2;           /* LBA high byte */
    uint8_t device;         /* Device register */
    
    uint8_t lba3;           /* LBA register (47:24) */
    uint8_t lba4;           /* LBA register (55:48) */
    uint8_t lba5;           /* LBA register (63:56) */
    uint8_t feature_high;   /* Feature register high */
    
    uint8_t count_low;      /* Count register low */
    uint8_t count_high;     /* Count register high */
    uint8_t icc;            /* Isochronous Command Completion */
    uint8_t control;        /* Control register */
    
    uint8_t reserved1[4];
} __attribute__((packed)) fis_reg_h2d_t;

/* ============ AHCI Drive Info ============ */

typedef struct {
    int port;               /* AHCI port number */
    int type;               /* SATA / ATAPI */
    uint64_t sectors;       /* Total sectors */
    char model[41];         /* Model string */
    char serial[21];        /* Serial number */
    int present;
} ahci_drive_t;

/* ============ AHCI API ============ */

/**
 * Initialize AHCI controller.
 */
int ahci_init(void);

/**
 * Probe for connected drives.
 */
int ahci_probe_drives(ahci_drive_t *drives, int max);

/**
 * Read sectors from drive.
 */
int ahci_read(int port, uint64_t lba, uint32_t count, void *buffer);

/**
 * Write sectors to drive.
 */
int ahci_write(int port, uint64_t lba, uint32_t count, const void *buffer);

/**
 * Get drive info.
 */
int ahci_identify(int port, ahci_drive_t *info);

/**
 * Flush drive cache - ensures all data written to persistent storage.
 * Critical for data integrity.
 */
int ahci_flush(int port);

#endif /* NEOLYX_AHCI_H */
