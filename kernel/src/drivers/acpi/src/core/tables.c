#include <stdint.h>
#include "../../include/acpi.h"

void* acpi_find_table(const char *sig) {
    // TODO: Scan RSDT/XSDT for table with signature sig
    return 0;
}

uint16_t acpi_get_slp_typ(void) {
    // TODO: Parse FADT, return SLP_TYP value for S3/S4/S5
    return 0;
} 