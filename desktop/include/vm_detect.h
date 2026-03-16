/*
 * NeolyxOS VM Detection Header
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXVM_DETECT_H
#define NXVM_DETECT_H

#include <stdbool.h>

typedef enum {
    VM_TYPE_BARE_METAL = 0,
    VM_TYPE_QEMU = 1,
    VM_TYPE_VIRTUALBOX = 2,
    VM_TYPE_VMWARE = 3,
    VM_TYPE_HYPERV = 4,
    VM_TYPE_KVM = 5,
    VM_TYPE_UNKNOWN = 99
} vm_type_t;

/* Detect VM type */
vm_type_t vm_detect(void);

/* Check if running in any VM */
bool vm_is_virtual(void);

/* Get VM type name for display */
const char* vm_get_name(void);

#endif /* NXVM_DETECT_H */
