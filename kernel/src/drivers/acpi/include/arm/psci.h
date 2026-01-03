#ifndef ACPI_ARM_PSCI_H
#define ACPI_ARM_PSCI_H

struct psci_ops {
    unsigned int (*cpu_suspend)(unsigned int state, unsigned long entry);
    unsigned int (*cpu_off)(void);
    unsigned int (*cpu_on)(unsigned long cpuid, unsigned long entry);
};

extern const struct psci_ops *psci_ops;

#endif // ACPI_ARM_PSCI_H 