// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#include <kernel/init.h>
#include <drivers/cpu/zen/zen_smp.h>
#include <stdint.h>

int zen_smp_init(void) {
    // TODO: Enable multi-core/multi-thread support for Zen
    return 0;
}

core_initcall(zen_smp_init); 