/*
 * NeolyxOS Security Policy Header
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 */

#ifndef _SECURITY_POLICY_H
#define _SECURITY_POLICY_H

#include <stdint.h>
#include <stdbool.h>

/* Security Restriction Flags */
#define SEC_RESTRICT_USB            (1 << 0)
#define SEC_RESTRICT_MODULES        (1 << 1)
#define SEC_RESTRICT_JIT            (1 << 2)
#define SEC_RESTRICT_NETWORK        (1 << 3)
#define SEC_RESTRICT_DEBUG          (1 << 4)
#define SEC_RESTRICT_PERIPHERALS    (1 << 5)

#define SEC_POLICY_STRICT           0x3F
#define SEC_POLICY_STANDARD         0x00

/* Initialization */
void security_policy_init(void);

/* Control */
int security_policy_enable(uint32_t restrictions);
int security_policy_disable(void);

/* Status */
bool security_policy_is_enabled(void);
bool security_restriction_active(uint32_t restriction);
uint32_t security_get_restrictions(void);

/* Subsystem checks */
bool security_check_usb(uint16_t vendor_id, uint16_t product_id);
bool security_check_module(const char *module_name, bool is_signed);
bool security_check_jit(void);
bool security_check_debug(int target_pid);
bool security_check_network(uint32_t dest_ip, uint16_t dest_port);

/* Display */
void security_policy_status(void);

#endif /* _SECURITY_POLICY_H */
