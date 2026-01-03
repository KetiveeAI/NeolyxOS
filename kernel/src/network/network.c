#include "network/network.h"

extern void serial_puts(const char *s);

int network_init(void) {
    serial_puts("[NET] Network subsystem: Not implemented yet\n");
    return 0;
} 