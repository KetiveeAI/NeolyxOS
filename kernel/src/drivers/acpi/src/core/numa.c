#include "../../include/numa.h"

static struct acpi_numa_node nodes[8];
static int node_count = 0;

void acpi_parse_numa(void) {
    // TODO: Parse SRAT table and fill nodes[]
    node_count = 1;
    nodes[0].id = 0;
    nodes[0].base = 0;
    nodes[0].length = 0x80000000; // Example: 2GB
}

int acpi_numa_get_node_count(void) { return node_count; }
const struct acpi_numa_node* acpi_numa_get_node(int idx) {
    if (idx < 0 || idx >= node_count) return 0;
    return &nodes[idx];
} 