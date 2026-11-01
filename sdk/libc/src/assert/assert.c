/*
 * nxlibc - assertion failure report.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <nx/libc.h>
#include "../internal.h"

_Noreturn void __nxc_assert_fail(const char *expr, const char *file, int line, const char *func) {
    const char *e = expr ? expr : "?";
    const char *f = file ? file : "?";
    const char *fn = func ? func : "?";
    /* The debug log always gets it, even if stderr is a file or broken */
    nxc_debugf("Assertion failed: %s, file %s, line %d, function %s\n", e, f, line, fn);
    if (!__nxc_stderr_is_log()) {
        fprintf(stderr, "Assertion failed: %s, file %s, line %d, function %s\n", e, f, line, fn);
        fflush(stderr);
    }
    abort();
}
