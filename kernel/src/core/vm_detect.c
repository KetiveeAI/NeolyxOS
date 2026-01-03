/*
 * vm_detect.c - Virtual Machine Detection for NeolyxOS
 * 
 * Detects if the system is running in a virtual machine
 * using CPUID and other hardware fingerprinting techniques.
 * 
 * Based on XNU/FreeBSD detection methods.
 */

#include <stdint.h>
#include <stdbool.h>

/* Hypervisor vendor strings */
#define HYPERV_VENDOR_VMWARE     "VMwareVMware"
#define HYPERV_VENDOR_KVM        "KVMKVMKVM\0\0\0"
#define HYPERV_VENDOR_MICROSOFT  "Microsoft Hv"
#define HYPERV_VENDOR_XEN        "XenVMMXenVMM"
#define HYPERV_VENDOR_PARALLELS  "prl hyperv  "
#define HYPERV_VENDOR_VBOX       "VBoxVBoxVBox"
#define HYPERV_VENDOR_QEMU       "TCGTCGTCGTCG"  /* TCG = QEMU's Tiny Code Generator */
#define HYPERV_VENDOR_BHYVE      "bhyve bhyve "
#define HYPERV_VENDOR_APPLE      "VirtualApple"  /* Apple Virtualization.framework */

/* VM detection result structure */
typedef struct {
    bool is_virtual;           /* True if running in VM */
    bool hypervisor_present;   /* CPUID hypervisor bit set */
    char vendor[13];           /* Hypervisor vendor string */
    const char *vendor_name;   /* Human-readable vendor name */
    uint32_t hypervisor_features;  /* Hypervisor-specific features */
} vm_info_t;

/* Global VM info - populated at boot */
static vm_info_t g_vm_info = {0};
static bool g_vm_detected = false;

/* External serial for debugging */
extern void serial_puts(const char *s);

/*
 * cpuid_check_hypervisor - Check CPUID for hypervisor presence
 * 
 * CPUID leaf 1, ECX bit 31 indicates hypervisor presence.
 * This is the standard way all major hypervisors signal their presence.
 */
static bool cpuid_check_hypervisor(void) {
    uint32_t eax, ebx, ecx, edx;
    
    /* CPUID leaf 1: Feature Information */
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    
    /* Bit 31 of ECX = Hypervisor present */
    return (ecx >> 31) & 1;
}

/*
 * cpuid_get_hypervisor_vendor - Get hypervisor vendor string
 * 
 * CPUID leaf 0x40000000 returns hypervisor vendor ID string
 * in EBX, ECX, EDX (12 characters total).
 */
static void cpuid_get_hypervisor_vendor(char *vendor) {
    uint32_t eax, ebx, ecx, edx;
    
    /* CPUID leaf 0x40000000: Hypervisor identification */
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x40000000)
    );
    
    /* Vendor string in EBX, ECX, EDX */
    *(uint32_t *)(vendor + 0) = ebx;
    *(uint32_t *)(vendor + 4) = ecx;
    *(uint32_t *)(vendor + 8) = edx;
    vendor[12] = '\0';
}

/*
 * identify_hypervisor - Match vendor string to known hypervisors
 */
static const char* identify_hypervisor(const char *vendor) {
    /* Compare first 12 bytes */
    if (__builtin_memcmp(vendor, HYPERV_VENDOR_VMWARE, 12) == 0)
        return "VMware";
    if (__builtin_memcmp(vendor, HYPERV_VENDOR_KVM, 12) == 0)
        return "KVM";
    if (__builtin_memcmp(vendor, HYPERV_VENDOR_MICROSOFT, 12) == 0)
        return "Hyper-V";
    if (__builtin_memcmp(vendor, HYPERV_VENDOR_XEN, 12) == 0)
        return "Xen";
    if (__builtin_memcmp(vendor, HYPERV_VENDOR_PARALLELS, 12) == 0)
        return "Parallels";
    if (__builtin_memcmp(vendor, HYPERV_VENDOR_VBOX, 12) == 0)
        return "VirtualBox";
    if (__builtin_memcmp(vendor, HYPERV_VENDOR_QEMU, 12) == 0)
        return "QEMU/TCG";
    if (__builtin_memcmp(vendor, HYPERV_VENDOR_BHYVE, 12) == 0)
        return "bhyve";
    if (__builtin_memcmp(vendor, HYPERV_VENDOR_APPLE, 12) == 0)
        return "Apple Virtualization";
    
    return "Unknown Hypervisor";
}

/*
 * vm_detect_init - Initialize VM detection
 * 
 * Call this early in boot to detect if running in a VM.
 * Results are cached for later queries.
 */
void vm_detect_init(void) {
    serial_puts("[VM] Detecting virtual machine environment...\n");
    
    /* Check hypervisor presence bit */
    g_vm_info.hypervisor_present = cpuid_check_hypervisor();
    
    if (g_vm_info.hypervisor_present) {
        g_vm_info.is_virtual = true;
        
        /* Get hypervisor vendor string */
        cpuid_get_hypervisor_vendor(g_vm_info.vendor);
        
        /* Identify the hypervisor */
        g_vm_info.vendor_name = identify_hypervisor(g_vm_info.vendor);
        
        serial_puts("[VM] Running in virtual machine: ");
        serial_puts(g_vm_info.vendor_name);
        serial_puts("\n");
    } else {
        g_vm_info.is_virtual = false;
        g_vm_info.vendor_name = "Bare Metal";
        serial_puts("[VM] Running on bare metal hardware\n");
    }
    
    g_vm_detected = true;
}

/*
 * vm_is_virtual - Check if running in a VM
 * 
 * Returns true if running in any virtual machine.
 */
bool vm_is_virtual(void) {
    if (!g_vm_detected) {
        vm_detect_init();
    }
    return g_vm_info.is_virtual;
}

/*
 * vm_get_hypervisor_name - Get hypervisor name
 * 
 * Returns human-readable hypervisor name or "Bare Metal".
 */
const char* vm_get_hypervisor_name(void) {
    if (!g_vm_detected) {
        vm_detect_init();
    }
    return g_vm_info.vendor_name;
}

/*
 * vm_get_info - Get full VM detection info
 * 
 * Returns pointer to the cached VM info structure.
 */
const vm_info_t* vm_get_info(void) {
    if (!g_vm_detected) {
        vm_detect_init();
    }
    return &g_vm_info;
}

/*
 * vm_is_kvm - Check if running in KVM/QEMU
 */
bool vm_is_kvm(void) {
    if (!g_vm_detected) vm_detect_init();
    return g_vm_info.is_virtual && 
           (__builtin_memcmp(g_vm_info.vendor, HYPERV_VENDOR_KVM, 12) == 0 ||
            __builtin_memcmp(g_vm_info.vendor, HYPERV_VENDOR_QEMU, 12) == 0);
}

/*
 * vm_is_vmware - Check if running in VMware
 */
bool vm_is_vmware(void) {
    if (!g_vm_detected) vm_detect_init();
    return g_vm_info.is_virtual && 
           __builtin_memcmp(g_vm_info.vendor, HYPERV_VENDOR_VMWARE, 12) == 0;
}

/*
 * vm_is_hyperv - Check if running in Hyper-V
 */
bool vm_is_hyperv(void) {
    if (!g_vm_detected) vm_detect_init();
    return g_vm_info.is_virtual && 
           __builtin_memcmp(g_vm_info.vendor, HYPERV_VENDOR_MICROSOFT, 12) == 0;
}

/*
 * vm_is_virtualbox - Check if running in VirtualBox
 */
bool vm_is_virtualbox(void) {
    if (!g_vm_detected) vm_detect_init();
    return g_vm_info.is_virtual && 
           __builtin_memcmp(g_vm_info.vendor, HYPERV_VENDOR_VBOX, 12) == 0;
}

/*
 * vm_print_info - Print VM detection info to serial
 */
void vm_print_info(void) {
    if (!g_vm_detected) vm_detect_init();
    
    serial_puts("=== VM Detection Info ===\n");
    serial_puts("  Running in VM: ");
    serial_puts(g_vm_info.is_virtual ? "Yes" : "No");
    serial_puts("\n");
    
    if (g_vm_info.is_virtual) {
        serial_puts("  Hypervisor: ");
        serial_puts(g_vm_info.vendor_name);
        serial_puts("\n");
        serial_puts("  Vendor ID: ");
        serial_puts(g_vm_info.vendor);
        serial_puts("\n");
    }
    serial_puts("=========================\n");
}
