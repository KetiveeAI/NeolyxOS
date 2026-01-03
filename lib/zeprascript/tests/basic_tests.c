/*
 * ZebraScript Engine Tests
 * 
 * Basic tests for the JS engine.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#include "../include/zeprascript.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test macros */
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s...", #name); \
    test_##name(); \
    printf(" OK\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

/* ============================================================================
 * Engine Tests
 * ============================================================================ */

TEST(engine_create) {
    zs_engine *engine = zs_engine_create();
    assert(engine != NULL);
    
    const char *version = zs_engine_version();
    assert(version != NULL);
    assert(strcmp(version, ZS_VERSION_STRING) == 0);
    
    zs_engine_destroy(engine);
}

TEST(context_create) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    assert(ctx != NULL);
    
    zs_object *global = zs_context_global(ctx);
    assert(global != NULL);
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

TEST(context_userdata) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    int data = 42;
    zs_context_set_userdata(ctx, &data);
    
    int *retrieved = (int *)zs_context_get_userdata(ctx);
    assert(retrieved != NULL);
    assert(*retrieved == 42);
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

/* ============================================================================
 * Value Tests
 * ============================================================================ */

TEST(value_undefined) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    zs_value *val = zs_undefined(ctx);
    assert(val != NULL);
    assert(zs_typeof(val) == ZS_TYPE_UNDEFINED);
    assert(zs_is_undefined(val));
    assert(!zs_is_null(val));
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

TEST(value_null) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    zs_value *val = zs_null(ctx);
    assert(val != NULL);
    assert(zs_typeof(val) == ZS_TYPE_NULL);
    assert(zs_is_null(val));
    assert(!zs_is_undefined(val));
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

TEST(value_boolean) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    zs_value *true_val = zs_boolean(ctx, true);
    zs_value *false_val = zs_boolean(ctx, false);
    
    assert(zs_is_boolean(true_val));
    assert(zs_is_boolean(false_val));
    
    assert(zs_to_boolean(true_val) == true);
    assert(zs_to_boolean(false_val) == false);
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

TEST(value_number) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    zs_value *num = zs_number(ctx, 3.14159);
    assert(zs_is_number(num));
    
    double result = zs_to_number(num);
    assert(result > 3.14 && result < 3.15);
    
    zs_value *int_num = zs_integer(ctx, 42);
    assert(zs_to_integer(int_num) == 42);
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

TEST(value_string) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    zs_value *str = zs_string_len(ctx, "Hello, NeolyxOS!", 16);
    assert(zs_is_string(str));
    
    const char *result = zs_to_string(ctx, str);
    assert(result != NULL);
    assert(strcmp(result, "Hello, NeolyxOS!") == 0);
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

/* ============================================================================
 * Object Tests
 * ============================================================================ */

TEST(object_create) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    zs_object *obj = zs_object_create(ctx);
    assert(obj != NULL);
    assert(zs_is_object((zs_value *)obj));
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

TEST(array_create) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    zs_value *arr = zs_array_create(ctx, 10);
    assert(arr != NULL);
    assert(zs_is_array(arr));
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

/* ============================================================================
 * Function Tests
 * ============================================================================ */

static zs_value *native_add(zs_context *ctx, zs_value *this_val, 
                            int argc, zs_value **argv) {
    (void)this_val;
    
    if (argc < 2) return zs_number(ctx, 0);
    
    double a = zs_to_number(argv[0]);
    double b = zs_to_number(argv[1]);
    
    return zs_number(ctx, a + b);
}

TEST(function_create) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    zs_value *func = zs_function_create(ctx, native_add, "add", 2);
    assert(func != NULL);
    assert(zs_is_function(func));
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

TEST(function_call) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    zs_value *func = zs_function_create(ctx, native_add, "add", 2);
    
    zs_value *args[2] = {
        zs_number(ctx, 3),
        zs_number(ctx, 4)
    };
    
    zs_value *result = zs_call(ctx, func, NULL, 2, args);
    assert(result != NULL);
    assert(zs_is_number(result));
    assert(zs_to_number(result) == 7.0);
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

/* ============================================================================
 * GC Tests
 * ============================================================================ */

TEST(gc_collect) {
    zs_engine *engine = zs_engine_create();
    zs_context *ctx = zs_context_create(engine);
    
    /* Allocate many objects */
    for (int i = 0; i < 1000; i++) {
        zs_number(ctx, i);
    }
    
    /* Collect garbage */
    zs_gc_collect(engine);
    
    zs_gc_stats stats;
    zs_gc_get_stats(engine, &stats);
    assert(stats.gc_runs >= 1);
    
    zs_context_destroy(ctx);
    zs_engine_destroy(engine);
}

TEST(gc_mode) {
    zs_engine *engine = zs_engine_create();
    
    zs_jit_set_level(engine, ZS_JIT_OPTIMIZED);
    assert(zs_jit_get_level(engine) == ZS_JIT_OPTIMIZED);
    
    zs_gc_set_mode(engine, ZS_GC_LOW_MEMORY);
    
    zs_engine_destroy(engine);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("ZebraScript Engine Tests\n");
    printf("========================\n\n");
    
    /* Engine tests */
    printf("Engine:\n");
    RUN_TEST(engine_create);
    RUN_TEST(context_create);
    RUN_TEST(context_userdata);
    
    /* Value tests */
    printf("\nValues:\n");
    RUN_TEST(value_undefined);
    RUN_TEST(value_null);
    RUN_TEST(value_boolean);
    RUN_TEST(value_number);
    RUN_TEST(value_string);
    
    /* Object tests */
    printf("\nObjects:\n");
    RUN_TEST(object_create);
    RUN_TEST(array_create);
    
    /* Function tests */
    printf("\nFunctions:\n");
    RUN_TEST(function_create);
    RUN_TEST(function_call);
    
    /* GC tests */
    printf("\nGC:\n");
    RUN_TEST(gc_collect);
    RUN_TEST(gc_mode);
    
    printf("\n========================\n");
    printf("All %d tests passed!\n", tests_passed);
    
    return 0;
}
