#ifndef ACPI_H
#define ACPI_H

#include "power/sleep_states.h"
#include "x86/acpi_x86.h"
#include "numa.h"

struct acpi_pm_ops {
    int (*enter_sleep)(unsigned char state);
    void (*reset)(void);
    void (*poweroff)(void);
    int (*cpu_on)(unsigned int cpu, unsigned long entry);
};

extern struct acpi_pm_ops *acpi_power_ops;

void acpi_init(void);

#endif // ACPI_H 