/*
 * vm_detect.h - Virtual Machine Detection for NeolyxOS
 */

#ifndef _VM_DETECT_H
#define _VM_DETECT_H

#include <stdint.h>
#include <stdbool.h>

/* VM detection result structure */
typedef struct {
    bool is_virtual;           /* True if running in VM */
    bool hypervisor_present;   /* CPUID hypervisor bit set */
    char vendor[13];           /* Hypervisor vendor string */
    const char *vendor_name;   /* Human-readable vendor name */
    uint32_t hypervisor_features;
} vm_info_t;

/* Initialize VM detection (call early in boot) */
void vm_detect_init(void);

/* Check if running in a VM */
bool vm_is_virtual(void);

/* Get hypervisor name (e.g., "KVM", "VMware", "Bare Metal") */
const char* vm_get_hypervisor_name(void);

/* Get full VM detection info */
const vm_info_t* vm_get_info(void);

/* Check for specific hypervisors */
bool vm_is_kvm(void);
bool vm_is_vmware(void);
bool vm_is_hyperv(void);
bool vm_is_virtualbox(void);

/* Print VM info to serial console */
void vm_print_info(void);

#endif /* _VM_DETECT_H */
