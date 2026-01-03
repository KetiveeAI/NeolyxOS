#include "../../include/acpi.h"

struct acpi_pm_ops *acpi_power_ops = 0;

void acpi_init(void) {
    // Detect ACPI tables, initialize power ops
#if defined(CONFIG_X86_64)
    x86_acpi_init();
#endif
} 