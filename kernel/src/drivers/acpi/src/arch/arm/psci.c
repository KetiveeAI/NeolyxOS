#include "../../../include/arm/psci.h"

static unsigned int psci_cpu_suspend(unsigned int state, unsigned long entry) { return 0; }
static unsigned int psci_cpu_off(void) { return 0; }
static unsigned int psci_cpu_on(unsigned long cpuid, unsigned long entry) { return 0; }

const struct psci_ops psci_ops_impl = {
    .cpu_suspend = psci_cpu_suspend,
    .cpu_off = psci_cpu_off,
    .cpu_on = psci_cpu_on,
};

const struct psci_ops *psci_ops = &psci_ops_impl; 