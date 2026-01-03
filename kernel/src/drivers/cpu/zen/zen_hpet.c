// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#include <kernel/init.h>
#include <drivers/cpu/zen/zen_hpet.h>
#include <stdint.h>

int zen_hpet_init(void) {
    // TODO: High precision event timer for Zen
    return 0;
}

driver_initcall(zen_hpet_init); 