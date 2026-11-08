/*
 * nxlibc - <assert.h>
 *
 * assert(e) writes "Assertion failed: e, file F, line L, function f" to
 * stderr and the kernel debug log, then calls abort(). With NDEBUG defined
 * it does nothing (and does not evaluate e). This header may be included
 * more than once with NDEBUG toggled, as ISO C requires.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#undef assert

#ifdef NDEBUG
#define assert(ignore) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __nxc_assert_fail(#e, __FILE__, __LINE__, __func__))
#endif

#ifndef NXLIBC_ASSERT_H
#define NXLIBC_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

_Noreturn void __nxc_assert_fail(const char *expr, const char *file, int line, const char *func);

#ifdef __cplusplus
}
#endif

#if !defined(__cplusplus) && __STDC_VERSION__ >= 201112L && __STDC_VERSION__ < 202311L
#define static_assert _Static_assert
#endif

#endif /* NXLIBC_ASSERT_H */
