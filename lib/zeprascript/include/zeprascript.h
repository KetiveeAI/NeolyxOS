/*
 * ZebraScript - NeolyxOS JavaScript Engine
 * 
 * Shared JavaScript engine for all NeolyxOS applications.
 * Based on ZepraBrowser's ZebraScript engine.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef ZEPRASCRIPT_H
#define ZEPRASCRIPT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version Info
 * ============================================================================ */

#define ZS_VERSION_MAJOR    1
#define ZS_VERSION_MINOR    0
#define ZS_VERSION_PATCH    0
#define ZS_VERSION_STRING   "1.0.0"

/* ECMAScript compatibility level */
#define ZS_ECMA_VERSION     2024

/* ============================================================================
 * Core Types
 * ============================================================================ */

/* Opaque handles */
typedef struct zs_engine zs_engine;
typedef struct zs_context zs_context;
typedef struct zs_value zs_value;
typedef struct zs_object zs_object;
typedef struct zs_function zs_function;
typedef struct zs_string_t zs_string_t;

/* Value types */
typedef enum zs_value_type {
    ZS_TYPE_UNDEFINED = 0,
    ZS_TYPE_NULL,
    ZS_TYPE_BOOLEAN,
    ZS_TYPE_NUMBER,
    ZS_TYPE_STRING,
    ZS_TYPE_OBJECT,
    ZS_TYPE_FUNCTION,
    ZS_TYPE_SYMBOL,
    ZS_TYPE_BIGINT,
} zs_value_type;

/* Error types */
typedef enum zs_error {
    ZS_OK = 0,
    ZS_ERROR_SYNTAX,
    ZS_ERROR_REFERENCE,
    ZS_ERROR_TYPE,
    ZS_ERROR_RANGE,
    ZS_ERROR_INTERNAL,
    ZS_ERROR_OOM,
    ZS_ERROR_STACK_OVERFLOW,
} zs_error;

/* Native function callback */
typedef zs_value* (*zs_native_fn)(zs_context *ctx, zs_value *this_val, 
                                   int argc, zs_value **argv);

/* ============================================================================
 * Engine Lifecycle
 * ============================================================================ */

/* Create global engine instance */
zs_engine *zs_engine_create(void);

/* Destroy engine and free all resources */
void zs_engine_destroy(zs_engine *engine);

/* Get engine version string */
const char *zs_engine_version(void);

/* ============================================================================
 * Context Management
 * ============================================================================ */

/* Create execution context within engine */
zs_context *zs_context_create(zs_engine *engine);

/* Destroy context */
void zs_context_destroy(zs_context *ctx);

/* Get global object for context */
zs_object *zs_context_global(zs_context *ctx);

/* Set user data on context */
void zs_context_set_userdata(zs_context *ctx, void *data);
void *zs_context_get_userdata(zs_context *ctx);

/* ============================================================================
 * Script Evaluation
 * ============================================================================ */

/* Evaluate JavaScript code */
zs_value *zs_eval(zs_context *ctx, const char *code);

/* Evaluate with filename for error reporting */
zs_value *zs_eval_file(zs_context *ctx, const char *code, const char *filename);

/* Compile script without executing */
zs_function *zs_compile(zs_context *ctx, const char *code, const char *filename);

/* Execute compiled function */
zs_value *zs_execute(zs_context *ctx, zs_function *func);

/* Check for pending exception */
bool zs_has_exception(zs_context *ctx);

/* Get and clear exception */
zs_value *zs_get_exception(zs_context *ctx);

/* ============================================================================
 * Value Creation
 * ============================================================================ */

/* Primitives */
zs_value *zs_undefined(zs_context *ctx);
zs_value *zs_null(zs_context *ctx);
zs_value *zs_boolean(zs_context *ctx, bool value);
zs_value *zs_number(zs_context *ctx, double value);
zs_value *zs_integer(zs_context *ctx, int64_t value);
zs_value *zs_new_string(zs_context *ctx, const char *str);
zs_value *zs_string_len(zs_context *ctx, const char *str, size_t len);

/* Objects */
zs_object *zs_object_create(zs_context *ctx);
zs_value *zs_array_create(zs_context *ctx, int length);

/* Functions */
zs_value *zs_function_create(zs_context *ctx, zs_native_fn fn, const char *name, int arg_count);

/* ============================================================================
 * Value Inspection
 * ============================================================================ */

/* Type checking */
zs_value_type zs_typeof(zs_value *val);
bool zs_is_undefined(zs_value *val);
bool zs_is_null(zs_value *val);
bool zs_is_boolean(zs_value *val);
bool zs_is_number(zs_value *val);
bool zs_is_string(zs_value *val);
bool zs_is_object(zs_value *val);
bool zs_is_function(zs_value *val);
bool zs_is_array(zs_value *val);

/* Value conversion */
bool zs_to_boolean(zs_value *val);
double zs_to_number(zs_value *val);
int64_t zs_to_integer(zs_value *val);
const char *zs_to_string(zs_context *ctx, zs_value *val);

/* ============================================================================
 * Object Operations
 * ============================================================================ */

/* Property access */
zs_value *zs_get_property(zs_context *ctx, zs_object *obj, const char *name);
void zs_set_property(zs_context *ctx, zs_object *obj, const char *name, zs_value *val);
bool zs_has_property(zs_object *obj, const char *name);
bool zs_delete_property(zs_object *obj, const char *name);

/* Array access */
zs_value *zs_get_index(zs_context *ctx, zs_value *arr, int index);
void zs_set_index(zs_context *ctx, zs_value *arr, int index, zs_value *val);
int zs_array_length(zs_value *arr);

/* Function calls */
zs_value *zs_call(zs_context *ctx, zs_value *func, zs_value *this_val, 
                  int argc, zs_value **argv);
zs_value *zs_call_method(zs_context *ctx, zs_object *obj, const char *method,
                          int argc, zs_value **argv);

/* ============================================================================
 * Native API Binding
 * ============================================================================ */

/* Register native function on global object */
void zs_register_global(zs_context *ctx, const char *name, zs_native_fn fn, int argc);

/* Register native module */
void zs_register_module(zs_context *ctx, const char *name, zs_object *exports);

/* Register native class */
typedef zs_value* (*zs_constructor_fn)(zs_context *ctx, int argc, zs_value **argv);
void zs_register_class(zs_context *ctx, const char *name, zs_constructor_fn ctor);

/* ============================================================================
 * Garbage Collection
 * ============================================================================ */

/* Trigger garbage collection */
void zs_gc_collect(zs_engine *engine);

/* Set GC mode */
typedef enum zs_gc_mode {
    ZS_GC_NORMAL,       /* Default incremental */
    ZS_GC_LOW_MEMORY,   /* Aggressive collection */
    ZS_GC_DISABLED,     /* Manual only */
} zs_gc_mode;
void zs_gc_set_mode(zs_engine *engine, zs_gc_mode mode);

/* Get GC stats */
typedef struct zs_gc_stats {
    size_t heap_size;
    size_t heap_used;
    size_t objects_allocated;
    size_t gc_runs;
} zs_gc_stats;
void zs_gc_get_stats(zs_engine *engine, zs_gc_stats *stats);

/* ============================================================================
 * JIT Configuration
 * ============================================================================ */

/* JIT optimization levels */
typedef enum zs_jit_level {
    ZS_JIT_OFF = 0,     /* Interpreter only */
    ZS_JIT_BASELINE,    /* Quick compilation */
    ZS_JIT_OPTIMIZED,   /* Full optimization */
} zs_jit_level;

void zs_jit_set_level(zs_engine *engine, zs_jit_level level);
zs_jit_level zs_jit_get_level(zs_engine *engine);

/* ============================================================================
 * LyxStore Integration
 * ============================================================================ */

/* Initialize LyxStore bindings */
void zs_init_lyxstore(zs_context *ctx);

/* NeolyxOS native API bindings */
void zs_init_nxapi(zs_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ZEPRASCRIPT_H */
