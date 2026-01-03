#ifndef ACPI_THERMAL_ENERGY_H
#define ACPI_THERMAL_ENERGY_H

struct cpu_energy {
    unsigned int dynamic_power;
    unsigned int static_power;
    unsigned int capacity;
};

void acpi_update_energy_model(void);

#endif // ACPI_THERMAL_ENERGY_H 