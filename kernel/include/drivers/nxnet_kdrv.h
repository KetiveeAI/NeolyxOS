/*
 * NXNetwork Kernel Driver (nxnet.kdrv)
 * 
 * NeolyxOS Native Network Kernel Driver
 * 
 * Architecture:
 *   [ NXNet Kernel Driver ] ← MMIO, DMA, IRQ
 *        ↕ Ring buffers
 *   [ nxnet-server ]         ← User-space TCP/IP stack
 *        ↕ IPC
 *   [ libnxnet.so ]          ← Socket API
 * 
 * Supports: Intel E1000, Virtio-Net (QEMU), RTL8139
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXNET_KDRV_H
#define NXNET_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXNET_KDRV_VERSION    "1.0.0"
#define NXNET_KDRV_ABI        1

/* ============ NIC Types ============ */

typedef enum {
    NXNET_NIC_NONE = 0,
    NXNET_NIC_E1000,        /* Intel E1000 */
    NXNET_NIC_E1000E,       /* Intel E1000E */
    NXNET_NIC_VIRTIO,       /* VirtIO-Net (QEMU) */
    NXNET_NIC_RTL8139,      /* Realtek RTL8139 */
    NXNET_NIC_RTL8169,      /* Realtek RTL8169 */
    NXNET_NIC_I225,         /* Intel I225 */
} nxnet_nic_type_t;

/* ============ Link Status ============ */

typedef enum {
    NXNET_LINK_DOWN = 0,
    NXNET_LINK_UP,
} nxnet_link_status_t;

typedef enum {
    NXNET_SPEED_UNKNOWN = 0,
    NXNET_SPEED_10,
    NXNET_SPEED_100,
    NXNET_SPEED_1000,
    NXNET_SPEED_2500,
    NXNET_SPEED_10000,
} nxnet_speed_t;

/* ============ MAC Address ============ */

typedef struct {
    uint8_t octets[6];
} nxnet_mac_t;

/* ============ NIC Device Info ============ */

typedef struct {
    uint32_t            id;
    char                name[64];
    char                vendor[32];
    nxnet_nic_type_t    type;
    nxnet_mac_t         mac;
    nxnet_link_status_t link;
    nxnet_speed_t       speed;
    uint8_t             pci_bus;
    uint8_t             pci_dev;
    uint8_t             pci_func;
    uint8_t             irq;
    uint64_t            mmio_base;
    size_t              mmio_size;
    
    /* Statistics */
    uint64_t            tx_packets;
    uint64_t            rx_packets;
    uint64_t            tx_bytes;
    uint64_t            rx_bytes;
    uint64_t            tx_errors;
    uint64_t            rx_errors;
} nxnet_device_info_t;

/* ============ Packet Buffer ============ */

#define NXNET_MTU           1500
#define NXNET_MAX_PACKET    (NXNET_MTU + 14 + 4)  /* + Ethernet header + CRC */

typedef struct {
    uint8_t     data[NXNET_MAX_PACKET];
    uint16_t    length;
    uint16_t    flags;
} nxnet_packet_t;

/* ============ Ring Buffer (kernel ↔ server) ============ */

#define NXNET_RX_RING_SIZE  256
#define NXNET_TX_RING_SIZE  256

typedef struct {
    nxnet_packet_t      packets[NXNET_RX_RING_SIZE];
    volatile uint32_t   head;
    volatile uint32_t   tail;
    volatile uint32_t   count;
} nxnet_rx_ring_t;

typedef struct {
    nxnet_packet_t      packets[NXNET_TX_RING_SIZE];
    volatile uint32_t   head;
    volatile uint32_t   tail;
    volatile uint32_t   count;
} nxnet_tx_ring_t;

/* ============ Kernel Driver API ============ */

/**
 * Initialize network hardware
 */
int nxnet_kdrv_init(void);

/**
 * Shutdown network
 */
void nxnet_kdrv_shutdown(void);

/**
 * Get number of NIC devices
 */
int nxnet_kdrv_device_count(void);

/**
 * Get device info
 */
int nxnet_kdrv_device_info(int index, nxnet_device_info_t *info);

/**
 * Bring interface up
 */
int nxnet_kdrv_up(int device);

/**
 * Bring interface down
 */
int nxnet_kdrv_down(int device);

/**
 * Transmit packet
 */
int nxnet_kdrv_tx(int device, const void *data, size_t len);

/**
 * Receive packet (non-blocking)
 */
int nxnet_kdrv_rx(int device, void *data, size_t max_len);

/**
 * Get link status
 */
nxnet_link_status_t nxnet_kdrv_link_status(int device);

/**
 * Set MAC address
 */
int nxnet_kdrv_set_mac(int device, const nxnet_mac_t *mac);

/**
 * Enable promiscuous mode
 */
int nxnet_kdrv_set_promisc(int device, int enable);

/**
 * Debug output
 */
void nxnet_kdrv_debug(void);

#endif /* NXNET_KDRV_H */
