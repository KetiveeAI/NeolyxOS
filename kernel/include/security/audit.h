/*
 * NeolyxOS Audit Header
 * 
 * Security event auditing and logging.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_AUDIT_H
#define NEOLYX_AUDIT_H

#include <stdint.h>

/* Event types */
#define AUDIT_TYPE_LOGIN    1
#define AUDIT_TYPE_NETWORK  2
#define AUDIT_TYPE_APP      3
#define AUDIT_TYPE_SYSTEM   4

/* Event results */
#define AUDIT_SUCCESS       0
#define AUDIT_FAILURE       1

/* API Functions */
void audit_init(void);
void audit_log(uint32_t type, uint32_t result, const char *msg);
void audit_log_user(uint32_t type, uint32_t result, const char *user, const char *msg);
void audit_dump(void);

#endif /* NEOLYX_AUDIT_H */
