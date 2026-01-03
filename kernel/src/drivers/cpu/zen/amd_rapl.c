// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#include <kernel/init.h>
#include <drivers/cpu/zen/amd_rapl.h>
#include <stdint.h>

int amd_rapl_init(void) {
    // TODO: Implement AMD RAPL power/energy counters
    return 0;
}

driver_initcall(amd_rapl_init); 