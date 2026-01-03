/*
 * NeolyxOS Kernel Stress Test Header
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYXOS_STRESS_TEST_H
#define NEOLYXOS_STRESS_TEST_H

/* Run all stress tests. Returns number of failures. */
int stress_test_run(void);

/* Run single test by name. Returns 0 on pass, non-zero on fail. */
int stress_test_single(const char *name);

#endif /* NEOLYXOS_STRESS_TEST_H */
