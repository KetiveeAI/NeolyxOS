// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.
#include <kernel/init.h>
#include <drivers/cpu/zen/zen_apic.h>
#include <stdint.h>

int zen_apic_init(void) {
    // TODO: Detect and initialize AMD Zen APIC
    return 0;
}

core_initcall(zen_apic_init); 