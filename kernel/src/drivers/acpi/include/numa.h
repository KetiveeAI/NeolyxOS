#ifndef ACPI_NUMA_H
#define ACPI_NUMA_H

struct acpi_numa_node {
    int id;
    unsigned long base;
    unsigned long length;
};

void acpi_parse_numa(void);
int acpi_numa_get_node_count(void);
const struct acpi_numa_node* acpi_numa_get_node(int idx);

#endif // ACPI_NUMA_H 