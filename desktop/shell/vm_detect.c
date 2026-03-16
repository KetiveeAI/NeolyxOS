/*
 * NeolyxOS VM Detection Module (Simplified)
 * 
 * Detects if running in a virtual machine using safe approach.
 * Always assumes VM for now to avoid CPUID issues.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include <stdbool.h>
#include "../include/vm_detect.h"

/* 
 * For safety, always assume we're in a VM in this version.
 * CPUID-based detection can be added later with proper testing.
 */

static vm_type_t g_vm_type = VM_TYPE_QEMU;  /* Default to QEMU (safe assumption for development) */

/* Detect VM type - simplified version */
vm_type_t vm_detect(void) {
    return g_vm_type;
}

/* Check if running in any VM */
bool vm_is_virtual(void) {
    return true;  /* Always true for safety during development */
}

/* Get VM type name for display */
const char* vm_get_name(void) {
    return "Virtual";  /* Generic name */
}
