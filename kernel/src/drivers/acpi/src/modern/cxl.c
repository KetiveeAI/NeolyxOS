#include <stdbool.h>

struct acpi_cxl {
    void *regs;
    bool supports_mem_persistence;
};

int acpi_cxl_init(void) {
    // TODO: Parse CEDT, initialize CXL host bridges
    return 0;
} 