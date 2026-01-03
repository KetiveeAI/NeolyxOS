/*
 * REOX Runtime Library - Implementation
 * Standard library for REOX compiled programs
 * Zero external dependencies (uses only libc)
 * 
 * Note: min/max/clamp/abs for int are defined inline in header for LTO
 */

#include "reox_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

/* ============================================================================
 * Print Functions
 * ============================================================================ */

void println(rx_string s) {
    if (RX_LIKELY(s != NULL)) {
        puts(s);
    } else {
        puts("(null)");
    }
}

void print(rx_string s) {
    if (RX_LIKELY(s != NULL)) {
        fputs(s, stdout);
    } else {
        fputs("(null)", stdout);
    }
}

void print_int(rx_int n) {
    printf("%ld", (long)n);
}

void print_float(rx_float n) {
    printf("%g", n);
}

void print_bool(rx_bool b) {
    fputs(b ? "true" : "false", stdout);
}

void printf_rx(rx_string fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/* ============================================================================
 * String Functions
 * ============================================================================ */

rx_str str_new(const char* s) {
    rx_str result;
    if (RX_UNLIKELY(s == NULL)) {
        result.data = NULL;
        result.len = 0;
        result.capacity = 0;
        return result;
    }
    
    size_t len = strlen(s);
    result.len = len;
    result.capacity = len + 1;
    result.data = (char*)malloc(result.capacity);
    if (RX_LIKELY(result.data != NULL)) {
        memcpy(result.data, s, len + 1);
    }
    return result;
}

rx_str str_with_capacity(size_t capacity) {
    rx_str result;
    result.len = 0;
    result.capacity = capacity > 0 ? capacity : 16;
    result.data = (char*)malloc(result.capacity);
    if (RX_LIKELY(result.data != NULL)) {
        result.data[0] = '\0';
    }
    return result;
}

void str_free(rx_str* s) {
    if (s && s->data) {
        free(s->data);
        s->data = NULL;
        s->len = 0;
        s->capacity = 0;
    }
}

size_t str_len(const rx_str* s) {
    return s ? s->len : 0;
}

rx_str str_concat(const rx_str* a, const rx_str* b) {
    rx_str result;
    size_t total_len = (a ? a->len : 0) + (b ? b->len : 0);
    result.len = total_len;
    result.capacity = total_len + 1;
    result.data = (char*)malloc(result.capacity);
    
    if (RX_LIKELY(result.data != NULL)) {
        size_t pos = 0;
        if (a && a->data) {
            memcpy(result.data, a->data, a->len);
            pos = a->len;
        }
        if (b && b->data) {
            memcpy(result.data + pos, b->data, b->len);
        }
        result.data[total_len] = '\0';
    }
    return result;
}

void str_append(rx_str* s, const char* suffix) {
    if (RX_UNLIKELY(!s || !suffix)) return;
    
    size_t suffix_len = strlen(suffix);
    size_t new_len = s->len + suffix_len;
    
    if (new_len + 1 > s->capacity) {
        size_t new_capacity = (new_len + 1) * 2;
        char* new_data = (char*)realloc(s->data, new_capacity);
        if (RX_UNLIKELY(!new_data)) return;
        s->data = new_data;
        s->capacity = new_capacity;
    }
    
    memcpy(s->data + s->len, suffix, suffix_len + 1);
    s->len = new_len;
}

int str_cmp(const rx_str* a, const rx_str* b) {
    if (!a || !a->data) return b && b->data ? -1 : 0;
    if (!b || !b->data) return 1;
    return strcmp(a->data, b->data);
}

bool str_eq(const rx_str* a, const rx_str* b) {
    return str_cmp(a, b) == 0;
}

rx_str str_substr(const rx_str* s, size_t start, size_t len) {
    rx_str result = {NULL, 0, 0};
    if (RX_UNLIKELY(!s || !s->data || start >= s->len)) return result;
    
    if (start + len > s->len) {
        len = s->len - start;
    }
    
    result.len = len;
    result.capacity = len + 1;
    result.data = (char*)malloc(result.capacity);
    if (RX_LIKELY(result.data != NULL)) {
        memcpy(result.data, s->data + start, len);
        result.data[len] = '\0';
    }
    return result;
}

int64_t str_find(const rx_str* s, const char* needle) {
    if (RX_UNLIKELY(!s || !s->data || !needle)) return -1;
    char* found = strstr(s->data, needle);
    return found ? (int64_t)(found - s->data) : -1;
}

bool str_contains(const rx_str* s, const char* needle) {
    return str_find(s, needle) >= 0;
}

bool str_starts_with(const rx_str* s, const char* prefix) {
    if (RX_UNLIKELY(!s || !s->data || !prefix)) return false;
    size_t prefix_len = strlen(prefix);
    if (prefix_len > s->len) return false;
    return memcmp(s->data, prefix, prefix_len) == 0;
}

bool str_ends_with(const rx_str* s, const char* suffix) {
    if (RX_UNLIKELY(!s || !s->data || !suffix)) return false;
    size_t suffix_len = strlen(suffix);
    if (suffix_len > s->len) return false;
    return memcmp(s->data + s->len - suffix_len, suffix, suffix_len) == 0;
}

rx_str str_to_upper(const rx_str* s) {
    rx_str result = {NULL, 0, 0};
    if (RX_UNLIKELY(!s || !s->data)) return result;
    
    result.len = s->len;
    result.capacity = s->len + 1;
    result.data = (char*)malloc(result.capacity);
    if (RX_LIKELY(result.data != NULL)) {
        for (size_t i = 0; i < s->len; i++) {
            result.data[i] = (char)toupper((unsigned char)s->data[i]);
        }
        result.data[s->len] = '\0';
    }
    return result;
}

rx_str str_to_lower(const rx_str* s) {
    rx_str result = {NULL, 0, 0};
    if (RX_UNLIKELY(!s || !s->data)) return result;
    
    result.len = s->len;
    result.capacity = s->len + 1;
    result.data = (char*)malloc(result.capacity);
    if (RX_LIKELY(result.data != NULL)) {
        for (size_t i = 0; i < s->len; i++) {
            result.data[i] = (char)tolower((unsigned char)s->data[i]);
        }
        result.data[s->len] = '\0';
    }
    return result;
}

rx_str str_trim(const rx_str* s) {
    rx_str result = {NULL, 0, 0};
    if (RX_UNLIKELY(!s || !s->data)) return result;
    
    size_t start = 0;
    while (start < s->len && isspace((unsigned char)s->data[start])) {
        start++;
    }
    
    size_t end = s->len;
    while (end > start && isspace((unsigned char)s->data[end - 1])) {
        end--;
    }
    
    return str_substr(s, start, end - start);
}

rx_array str_split(const rx_str* s, char delimiter) {
    rx_array result = array_new(sizeof(rx_str), 8);
    if (RX_UNLIKELY(!s || !s->data)) return result;
    
    size_t start = 0;
    for (size_t i = 0; i <= s->len; i++) {
        if (i == s->len || s->data[i] == delimiter) {
            rx_str part = str_substr(s, start, i - start);
            array_push(&result, &part);
            start = i + 1;
        }
    }
    return result;
}

rx_str int_to_str(rx_int n) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%ld", (long)n);
    return str_new(buffer);
}

rx_str float_to_str(rx_float n) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%g", n);
    return str_new(buffer);
}

rx_int str_to_int(const rx_str* s) {
    if (RX_UNLIKELY(!s || !s->data)) return 0;
    return (rx_int)atoll(s->data);
}

rx_float str_to_float(const rx_str* s) {
    if (RX_UNLIKELY(!s || !s->data)) return 0.0;
    return atof(s->data);
}

/* ============================================================================
 * Math Functions (libm wrappers)
 * Note: abs_int, min/max/clamp for int are inline in header
 * ============================================================================ */

rx_float abs_float(rx_float x) {
    return fabs(x);
}

rx_float pow_float(rx_float base, rx_float exp) {
    return pow(base, exp);
}

rx_float sqrt_float(rx_float x) {
    return sqrt(x);
}

rx_float cbrt_float(rx_float x) {
    return cbrt(x);
}

rx_float sin_float(rx_float x) {
    return sin(x);
}

rx_float cos_float(rx_float x) {
    return cos(x);
}

rx_float tan_float(rx_float x) {
    return tan(x);
}

rx_float asin_float(rx_float x) {
    return asin(x);
}

rx_float acos_float(rx_float x) {
    return acos(x);
}

rx_float atan_float(rx_float x) {
    return atan(x);
}

rx_float atan2_float(rx_float y, rx_float x) {
    return atan2(y, x);
}

rx_float exp_float(rx_float x) {
    return exp(x);
}

rx_float log_float(rx_float x) {
    return log(x);
}

rx_float log10_float(rx_float x) {
    return log10(x);
}

rx_float log2_float(rx_float x) {
    return log2(x);
}

rx_float floor_float(rx_float x) {
    return floor(x);
}

rx_float ceil_float(rx_float x) {
    return ceil(x);
}

rx_float round_float(rx_float x) {
    return round(x);
}

rx_float trunc_float(rx_float x) {
    return trunc(x);
}

/* ============================================================================
 * Memory Functions
 * ============================================================================ */

void* rx_alloc(size_t size) {
    return malloc(size);
}

void* rx_calloc(size_t count, size_t size) {
    return calloc(count, size);
}

void* rx_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

void rx_free(void* ptr) {
    free(ptr);
}

void rx_memcpy(void* dest, const void* src, size_t n) {
    memcpy(dest, src, n);
}

void rx_memset(void* ptr, int value, size_t n) {
    memset(ptr, value, n);
}

/* ============================================================================
 * Array Functions
 * ============================================================================ */

rx_array array_new(size_t elem_size, size_t initial_capacity) {
    rx_array arr;
    arr.elem_size = elem_size;
    arr.len = 0;
    arr.capacity = initial_capacity > 0 ? initial_capacity : 8;
    arr.data = malloc(arr.capacity * elem_size);
    return arr;
}

void array_free(rx_array* arr) {
    if (arr && arr->data) {
        free(arr->data);
        arr->data = NULL;
        arr->len = 0;
        arr->capacity = 0;
    }
}

size_t array_len(const rx_array* arr) {
    return arr ? arr->len : 0;
}

void array_push(rx_array* arr, const void* elem) {
    if (RX_UNLIKELY(!arr || !elem)) return;
    
    if (RX_UNLIKELY(arr->len >= arr->capacity)) {
        size_t new_capacity = arr->capacity * 2;
        void* new_data = realloc(arr->data, new_capacity * arr->elem_size);
        if (RX_UNLIKELY(!new_data)) return;
        arr->data = new_data;
        arr->capacity = new_capacity;
    }
    
    char* dest = (char*)arr->data + (arr->len * arr->elem_size);
    memcpy(dest, elem, arr->elem_size);
    arr->len++;
}

void array_pop(rx_array* arr, void* out_elem) {
    if (RX_UNLIKELY(!arr || arr->len == 0)) return;
    
    arr->len--;
    if (out_elem) {
        char* src = (char*)arr->data + (arr->len * arr->elem_size);
        memcpy(out_elem, src, arr->elem_size);
    }
}

void* array_get(rx_array* arr, size_t index) {
    if (RX_UNLIKELY(!arr || index >= arr->len)) return NULL;
    return (char*)arr->data + (index * arr->elem_size);
}

void array_set(rx_array* arr, size_t index, const void* elem) {
    if (RX_UNLIKELY(!arr || !elem || index >= arr->len)) return;
    char* dest = (char*)arr->data + (index * arr->elem_size);
    memcpy(dest, elem, arr->elem_size);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

RX_NORETURN void rx_exit(int code) {
    exit(code);
}

RX_NORETURN RX_COLD void rx_panic(rx_string message) {
    fprintf(stderr, "PANIC: %s\n", message ? message : "unknown error");
    exit(1);
}

void rx_assert(bool condition, rx_string message) {
    if (RX_UNLIKELY(!condition)) {
        fprintf(stderr, "ASSERTION FAILED: %s\n", message ? message : "assertion failed");
        exit(1);
    }
}
