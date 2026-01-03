// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#include <kernel/init.h>
#include <drivers/cpu/zen/amd_numa.h>
#include <stdint.h>

int amd_numa_init(void) {
    // TODO: NUMA-aware memory handling for Zen/EPYC
    return 0;
}

driver_initcall(amd_numa_init); 