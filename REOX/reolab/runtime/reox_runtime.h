/*
 * REOX Runtime Library - Header
 * Standard library for REOX compiled programs
 * Optimized for LTO and cross-module inlining
 */

#ifndef REOX_RUNTIME_H
#define REOX_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Compiler Hints
 * ============================================================================ */

#ifdef __GNUC__
#define RX_INLINE       static inline __attribute__((always_inline))
#define RX_NOINLINE     __attribute__((noinline))
#define RX_PURE         __attribute__((pure))
#define RX_CONST        __attribute__((const))
#define RX_HOT          __attribute__((hot))
#define RX_COLD         __attribute__((cold))
#define RX_LIKELY(x)    __builtin_expect(!!(x), 1)
#define RX_UNLIKELY(x)  __builtin_expect(!!(x), 0)
#define RX_NORETURN     __attribute__((noreturn))
#define RX_FLATTEN      __attribute__((flatten))
#else
#define RX_INLINE       static inline
#define RX_NOINLINE
#define RX_PURE
#define RX_CONST
#define RX_HOT
#define RX_COLD
#define RX_LIKELY(x)    (x)
#define RX_UNLIKELY(x)  (x)
#define RX_NORETURN
#define RX_FLATTEN
#endif

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

typedef int64_t rx_int;
typedef double rx_float;
typedef const char* rx_string;
typedef bool rx_bool;

/* String object (heap allocated) */
typedef struct rx_str {
    char* data;
    size_t len;
    size_t capacity;
} rx_str;

/* Array header */
typedef struct rx_array {
    void* data;
    size_t len;
    size_t capacity;
    size_t elem_size;
} rx_array;

/* ============================================================================
 * Print Functions (extern for shared lib, inlined for static)
 * ============================================================================ */

/* Print string with newline */
extern void println(rx_string s);

/* Print string without newline */
extern void print(rx_string s);

/* Print integer */
extern void print_int(rx_int n);

/* Print float */
extern void print_float(rx_float n);

/* Print boolean */
extern void print_bool(rx_bool b);

/* Print formatted */
extern void printf_rx(rx_string fmt, ...);

/* ============================================================================
 * Inline Math Functions (for maximum performance)
 * ============================================================================ */

RX_INLINE RX_CONST rx_int abs_int(rx_int x) {
    return x < 0 ? -x : x;
}

RX_INLINE RX_CONST rx_int min_int(rx_int a, rx_int b) {
    return a < b ? a : b;
}

RX_INLINE RX_CONST rx_int max_int(rx_int a, rx_int b) {
    return a > b ? a : b;
}

RX_INLINE RX_CONST rx_float min_float(rx_float a, rx_float b) {
    return a < b ? a : b;
}

RX_INLINE RX_CONST rx_float max_float(rx_float a, rx_float b) {
    return a > b ? a : b;
}

RX_INLINE RX_CONST rx_int clamp_int(rx_int x, rx_int min_val, rx_int max_val) {
    return x < min_val ? min_val : (x > max_val ? max_val : x);
}

RX_INLINE RX_CONST rx_float clamp_float(rx_float x, rx_float min_val, rx_float max_val) {
    return x < min_val ? min_val : (x > max_val ? max_val : x);
}

/* ============================================================================
 * Math Functions (extern - use libm)
 * ============================================================================ */

extern RX_PURE rx_float abs_float(rx_float x);
extern RX_PURE rx_float pow_float(rx_float base, rx_float exp);
extern RX_PURE rx_float sqrt_float(rx_float x);
extern RX_PURE rx_float cbrt_float(rx_float x);
extern RX_PURE rx_float sin_float(rx_float x);
extern RX_PURE rx_float cos_float(rx_float x);
extern RX_PURE rx_float tan_float(rx_float x);
extern RX_PURE rx_float asin_float(rx_float x);
extern RX_PURE rx_float acos_float(rx_float x);
extern RX_PURE rx_float atan_float(rx_float x);
extern RX_PURE rx_float atan2_float(rx_float y, rx_float x);
extern RX_PURE rx_float exp_float(rx_float x);
extern RX_PURE rx_float log_float(rx_float x);
extern RX_PURE rx_float log10_float(rx_float x);
extern RX_PURE rx_float log2_float(rx_float x);
extern RX_PURE rx_float floor_float(rx_float x);
extern RX_PURE rx_float ceil_float(rx_float x);
extern RX_PURE rx_float round_float(rx_float x);
extern RX_PURE rx_float trunc_float(rx_float x);

/* Constants */
#define RX_PI       3.14159265358979323846
#define RX_E        2.71828182845904523536
#define RX_TAU      6.28318530717958647692
#define RX_SQRT2    1.41421356237309504880

/* ============================================================================
 * String Functions
 * ============================================================================ */

extern rx_str str_new(const char* s);
extern rx_str str_with_capacity(size_t capacity);
extern void str_free(rx_str* s);
extern RX_PURE size_t str_len(const rx_str* s);
extern rx_str str_concat(const rx_str* a, const rx_str* b);
extern void str_append(rx_str* s, const char* suffix);
extern RX_PURE int str_cmp(const rx_str* a, const rx_str* b);
extern RX_PURE bool str_eq(const rx_str* a, const rx_str* b);
extern rx_str str_substr(const rx_str* s, size_t start, size_t len);
extern RX_PURE int64_t str_find(const rx_str* s, const char* needle);
extern RX_PURE bool str_contains(const rx_str* s, const char* needle);
extern RX_PURE bool str_starts_with(const rx_str* s, const char* prefix);
extern RX_PURE bool str_ends_with(const rx_str* s, const char* suffix);
extern rx_str str_to_upper(const rx_str* s);
extern rx_str str_to_lower(const rx_str* s);
extern rx_str str_trim(const rx_str* s);
extern rx_array str_split(const rx_str* s, char delimiter);
extern rx_str int_to_str(rx_int n);
extern rx_str float_to_str(rx_float n);
extern RX_PURE rx_int str_to_int(const rx_str* s);
extern RX_PURE rx_float str_to_float(const rx_str* s);

/* ============================================================================
 * Memory Functions (inline wrappers for performance)
 * ============================================================================ */

extern void* rx_alloc(size_t size);
extern void* rx_calloc(size_t count, size_t size);
extern void* rx_realloc(void* ptr, size_t size);
extern void rx_free(void* ptr);
extern void rx_memcpy(void* dest, const void* src, size_t n);
extern void rx_memset(void* ptr, int value, size_t n);

/* ============================================================================
 * Array Functions
 * ============================================================================ */

extern rx_array array_new(size_t elem_size, size_t initial_capacity);
extern void array_free(rx_array* arr);
extern RX_PURE size_t array_len(const rx_array* arr);
extern void array_push(rx_array* arr, const void* elem);
extern void array_pop(rx_array* arr, void* out_elem);
extern RX_HOT void* array_get(rx_array* arr, size_t index);
extern void array_set(rx_array* arr, size_t index, const void* elem);

/* Inline array length for hot paths */
RX_INLINE RX_PURE size_t array_len_fast(const rx_array* arr) {
    return arr ? arr->len : 0;
}

/* Inline bounds-unchecked array access for hot paths */
RX_INLINE RX_HOT void* array_get_unchecked(rx_array* arr, size_t index) {
    return (char*)arr->data + (index * arr->elem_size);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

extern RX_NORETURN void rx_exit(int code);
extern RX_NORETURN RX_COLD void rx_panic(rx_string message);
extern void rx_assert(bool condition, rx_string message);

/* ============================================================================
 * Fast Branch Hints
 * ============================================================================ */

RX_INLINE void rx_assume(bool cond) {
#ifdef __GNUC__
    if (!cond) __builtin_unreachable();
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* REOX_RUNTIME_H */
