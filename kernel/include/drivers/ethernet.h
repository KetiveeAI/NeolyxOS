#ifndef ETHERNET_H
#define ETHERNET_H

#include <stdint.h>
#include <stddef.h>
#include "drivers.h"

// Ethernet driver functions
int ethernet_driver_init(kernel_driver_t* drv);
int ethernet_driver_deinit(kernel_driver_t* drv);

// Ethernet operations
int ethernet_send_packet(const void* data, size_t len);
int ethernet_receive_packet(void* buffer, size_t max_len);
int ethernet_get_mac_address(uint8_t* mac);
int ethernet_is_ready(void);

// Ethernet frame types
#define ETHERNET_TYPE_IPV4    0x0800
#define ETHERNET_TYPE_ARP     0x0806
#define ETHERNET_TYPE_IPV6    0x86DD

// Ethernet constants
#define ETHERNET_HEADER_SIZE  14
#define ETHERNET_MAX_PAYLOAD  1500
#define ETHERNET_MIN_FRAME    60

#endif // ETHERNET_H 