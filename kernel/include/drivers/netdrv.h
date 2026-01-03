/*
 * NeolyxOS Extended Network Driver Support
 * 
 * Supports:
 *   - Intel (e1000, i40e, ixgbe, ice)
 *   - AMD/Realtek (rtl8139, rtl8169)
 *   - Virtio-net (VM optimized)
 *   - Multiple interfaces per machine
 *   - Bonding/teaming for high availability
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_NETWORK_DRIVERS_H
#define NEOLYX_NETWORK_DRIVERS_H

#include <stdint.h>
#include "net/network.h"

/* ============ Supported Devices ============ */

/* Intel */
#define PCI_VENDOR_INTEL        0x8086

#define INTEL_DEV_E1000         0x100E  /* 82540EM */
#define INTEL_DEV_E1000E        0x100F  /* 82545EM */
#define INTEL_DEV_82574L        0x10D3
#define INTEL_DEV_82579LM       0x1502
#define INTEL_DEV_I210          0x1533
#define INTEL_DEV_I211          0x1539
#define INTEL_DEV_I217_LM       0x153A
#define INTEL_DEV_I218_LM       0x155A
#define INTEL_DEV_I219_LM       0x156F
#define INTEL_DEV_I350          0x1521
#define INTEL_DEV_X540          0x1528
#define INTEL_DEV_X550          0x1563
#define INTEL_DEV_I40E          0x1572  /* 40 GbE */
#define INTEL_DEV_ICE           0x1592  /* 100 GbE */

/* AMD/Realtek */
#define PCI_VENDOR_REALTEK      0x10EC

#define REALTEK_DEV_8139        0x8139  /* 10/100 Mbps */
#define REALTEK_DEV_8169        0x8169  /* 1 Gbps */
#define REALTEK_DEV_8101        0x8136
#define REALTEK_DEV_8168        0x8168  /* Common in consumer PCs */

/* AMD */
#define PCI_VENDOR_AMD          0x1022

#define AMD_DEV_PCNET32         0x2000  /* PCnet32 */

/* VirtIO (VM optimized) */
#define PCI_VENDOR_VIRTIO       0x1AF4
#define PCI_VENDOR_QEMU         0x1AF4

#define VIRTIO_DEV_NET          0x1000  /* VirtIO Network */
#define VIRTIO_DEV_NET_MODERN   0x1041  /* VirtIO 1.0+ Network */

/* VMware */
#define PCI_VENDOR_VMWARE       0x15AD

#define VMWARE_DEV_VMXNET3      0x07B0  /* VMXNET3 */

/* ============ Driver Types ============ */

typedef enum {
    NET_DRIVER_E1000,           /* Intel 1G */
    NET_DRIVER_I40E,            /* Intel 10/40G */
    NET_DRIVER_ICE,             /* Intel 100G */
    NET_DRIVER_RTL8139,         /* Realtek 10/100 */
    NET_DRIVER_RTL8169,         /* Realtek 1G */
    NET_DRIVER_VIRTIO,          /* VirtIO (VM) */
    NET_DRIVER_VMXNET3,         /* VMware */
    NET_DRIVER_PCNET32,         /* AMD PCnet32 */
} net_driver_type_t;

/* ============ Multi-Interface Support ============ */

#define MAX_NET_INTERFACES      16

typedef struct {
    net_interface_t *interfaces[MAX_NET_INTERFACES];
    int count;
} net_interface_list_t;

/* ============ Interface Bonding ============ */

typedef enum {
    BOND_MODE_ROUNDROBIN = 0,   /* Balance load */
    BOND_MODE_ACTIVE_BACKUP,    /* Failover */
    BOND_MODE_BALANCE_XOR,      /* XOR hash */
    BOND_MODE_BROADCAST,        /* Send on all */
    BOND_MODE_802_3AD,          /* LACP */
    BOND_MODE_BALANCE_TLB,      /* Adaptive TX */
    BOND_MODE_BALANCE_ALB,      /* Adaptive RX/TX */
} bond_mode_t;

typedef struct {
    char name[16];              /* "bond0" */
    bond_mode_t mode;
    net_interface_t *slaves[8];
    int slave_count;
    int active_slave;
    net_interface_t interface;  /* Virtual interface */
} net_bond_t;

/* ============ Driver Detection API ============ */

/**
 * Initialize all network drivers.
 */
void netdrv_init(void);

/**
 * Probe for supported devices.
 */
int netdrv_probe(void);

/**
 * Get detected driver type for a PCI device.
 */
net_driver_type_t netdrv_identify(uint16_t vendor, uint16_t device);

/**
 * Check if virtualized (optimize for VM).
 */
int netdrv_is_virtualized(void);

/* ============ Multi-Interface API ============ */

/**
 * Get all interfaces.
 */
int netdrv_get_interfaces(net_interface_list_t *list);

/**
 * Create a bond interface.
 */
net_bond_t *netdrv_create_bond(const char *name, bond_mode_t mode);

/**
 * Add slave to bond.
 */
int netdrv_bond_add_slave(net_bond_t *bond, net_interface_t *slave);

/**
 * Remove slave from bond.
 */
int netdrv_bond_remove_slave(net_bond_t *bond, net_interface_t *slave);

#endif /* NEOLYX_NETWORK_DRIVERS_H */
