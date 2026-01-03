/*
 * NeolyxOS Intel e1000 Ethernet Driver
 * 
 * Supports Intel 8254x/8256x/8257x Gigabit Ethernet.
 * Common in virtual machines (QEMU, VirtualBox, VMware).
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_E1000_H
#define NEOLYX_E1000_H

#include <stdint.h>
#include "net/network.h"

/* ============ Supported Device IDs ============ */

#define E1000_VENDOR_ID     0x8086  /* Intel */

/* Device IDs */
#define E1000_DEV_82540EM   0x100E  /* QEMU default */
#define E1000_DEV_82545EM   0x100F  /* VMware */
#define E1000_DEV_82574L    0x10D3
#define E1000_DEV_82579LM   0x1502

/* ============ Register Offsets ============ */

#define E1000_CTRL      0x0000  /* Device Control */
#define E1000_STATUS    0x0008  /* Device Status */
#define E1000_EECD      0x0010  /* EEPROM Control */
#define E1000_EERD      0x0014  /* EEPROM Read */
#define E1000_ICR       0x00C0  /* Interrupt Cause Read */
#define E1000_ICS       0x00C8  /* Interrupt Cause Set */
#define E1000_IMS       0x00D0  /* Interrupt Mask Set */
#define E1000_IMC       0x00D8  /* Interrupt Mask Clear */

/* Receive */
#define E1000_RCTL      0x0100  /* Receive Control */
#define E1000_RDBAL     0x2800  /* RX Descriptor Base Low */
#define E1000_RDBAH     0x2804  /* RX Descriptor Base High */
#define E1000_RDLEN     0x2808  /* RX Descriptor Length */
#define E1000_RDH       0x2810  /* RX Descriptor Head */
#define E1000_RDT       0x2818  /* RX Descriptor Tail */

/* Transmit */
#define E1000_TCTL      0x0400  /* Transmit Control */
#define E1000_TDBAL     0x3800  /* TX Descriptor Base Low */
#define E1000_TDBAH     0x3804  /* TX Descriptor Base High */
#define E1000_TDLEN     0x3808  /* TX Descriptor Length */
#define E1000_TDH       0x3810  /* TX Descriptor Head */
#define E1000_TDT       0x3818  /* TX Descriptor Tail */

/* MAC Address */
#define E1000_RAL       0x5400  /* Receive Address Low */
#define E1000_RAH       0x5404  /* Receive Address High */
#define E1000_MTA       0x5200  /* Multicast Table Array */

/* ============ Control Register Bits ============ */

#define E1000_CTRL_FD       0x00000001  /* Full Duplex */
#define E1000_CTRL_LRST     0x00000008  /* Link Reset */
#define E1000_CTRL_ASDE     0x00000020  /* Auto-Speed Detection */
#define E1000_CTRL_SLU      0x00000040  /* Set Link Up */
#define E1000_CTRL_RST      0x04000000  /* Device Reset */

/* ============ RCTL Register Bits ============ */

#define E1000_RCTL_EN       0x00000002  /* Receiver Enable */
#define E1000_RCTL_SBP      0x00000004  /* Store Bad Packets */
#define E1000_RCTL_UPE      0x00000008  /* Unicast Promiscuous */
#define E1000_RCTL_MPE      0x00000010  /* Multicast Promiscuous */
#define E1000_RCTL_BAM      0x00008000  /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2K 0x00000000  /* Buffer Size 2048 */
#define E1000_RCTL_SECRC    0x04000000  /* Strip CRC */

/* ============ TCTL Register Bits ============ */

#define E1000_TCTL_EN       0x00000002  /* Transmitter Enable */
#define E1000_TCTL_PSP      0x00000008  /* Pad Short Packets */
#define E1000_TCTL_CT       0x00000FF0  /* Collision Threshold */
#define E1000_TCTL_COLD     0x003FF000  /* Collision Distance */

/* ============ Interrupt Bits ============ */

#define E1000_ICR_TXDW      0x00000001  /* TX Descriptor Written Back */
#define E1000_ICR_TXQE      0x00000002  /* TX Queue Empty */
#define E1000_ICR_LSC       0x00000004  /* Link Status Change */
#define E1000_ICR_RXT0      0x00000080  /* RX Timer Interrupt */

/* ============ Descriptors ============ */

/* Receive Descriptor */
typedef struct {
    uint64_t addr;      /* Buffer address */
    uint16_t length;    /* Packet length */
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

/* Transmit Descriptor */
typedef struct {
    uint64_t addr;      /* Buffer address */
    uint16_t length;    /* Packet length */
    uint8_t cso;        /* Checksum offset */
    uint8_t cmd;        /* Command */
    uint8_t status;     /* Status */
    uint8_t css;        /* Checksum start */
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

/* Descriptor Status */
#define E1000_RXD_STAT_DD   0x01    /* Descriptor Done */
#define E1000_RXD_STAT_EOP  0x02    /* End of Packet */

#define E1000_TXD_CMD_EOP   0x01    /* End of Packet */
#define E1000_TXD_CMD_IFCS  0x02    /* Insert FCS */
#define E1000_TXD_CMD_RS    0x08    /* Report Status */
#define E1000_TXD_STAT_DD   0x01    /* Descriptor Done */

/* ============ Driver State ============ */

#define E1000_NUM_RX_DESC   32
#define E1000_NUM_TX_DESC   32
#define E1000_RX_BUF_SIZE   2048

typedef struct {
    volatile uint8_t *mmio_base;
    
    /* Descriptors */
    e1000_rx_desc_t *rx_descs;
    e1000_tx_desc_t *tx_descs;
    uint32_t rx_cur;
    uint32_t tx_cur;
    
    /* RX Buffers */
    uint8_t *rx_buffers[E1000_NUM_RX_DESC];
    
    /* PCI info */
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    
    /* MAC */
    mac_addr_t mac;
} e1000_device_t;

/* ============ E1000 API ============ */

/**
 * Initialize e1000 driver, scan for devices.
 */
int e1000_init(void);

/**
 * Get detected e1000 device.
 */
e1000_device_t *e1000_get_device(int index);

/**
 * Send a packet.
 */
int e1000_send(e1000_device_t *dev, const void *data, uint32_t len);

/**
 * Receive a packet (polling).
 */
int e1000_receive(e1000_device_t *dev, void *buf, uint32_t max_len);

/**
 * Get MAC address.
 */
void e1000_get_mac(e1000_device_t *dev, mac_addr_t *mac);

/**
 * Handle interrupt.
 */
void e1000_irq_handler(void);

#endif /* NEOLYX_E1000_H */
