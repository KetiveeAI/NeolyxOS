/*
 * ZebraScript Engine - Core Implementation
 * 
 * Engine initialization, context management, and core operations.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#include "../include/zeprascript.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

struct zs_engine {
    int context_count;
    zs_context **contexts;
    zs_gc_mode gc_mode;
    zs_jit_level jit_level;
    zs_gc_stats gc_stats;
    void *heap;
    size_t heap_size;
};

struct zs_context {
    zs_engine *engine;
    zs_object *global;
    zs_value *exception;
    void *user_data;
    /* Bytecode cache */
    void *bytecode_cache;
    /* Call stack */
    void *call_stack;
    int stack_depth;
};

struct zs_value {
    zs_value_type type;
    union {
        bool boolean;
        double number;
        int64_t integer;
        zs_string_t *string;
        zs_object *object;
        zs_function *function;
    } as;
    /* GC markers */
    bool marked;
    struct zs_value *next;
};

struct zs_object {
    zs_value base;
    /* Property hash table */
    void *properties;
    int property_count;
    /* Prototype chain */
    zs_object *prototype;
    /* Hidden class for inline caching */
    void *hidden_class;
};

struct zs_string_t {
    zs_value base;
    char *data;
    size_t length;
    uint32_t hash;
};

struct zs_function {
    zs_value base;
    const char *name;
    int arg_count;
    bool is_native;
    union {
        zs_native_fn native;
        void *bytecode;
    } code;
    zs_object *scope;
};

/* ============================================================================
 * Engine Lifecycle
 * ============================================================================ */

zs_engine *zs_engine_create(void) {
    zs_engine *engine = (zs_engine *)malloc(sizeof(zs_engine));
    if (!engine) return NULL;
    
    memset(engine, 0, sizeof(zs_engine));
    engine->gc_mode = ZS_GC_NORMAL;
    engine->jit_level = ZS_JIT_BASELINE;
    
    /* Initialize heap (16 MB default) */
    engine->heap_size = 16 * 1024 * 1024;
    engine->heap = malloc(engine->heap_size);
    
    return engine;
}

void zs_engine_destroy(zs_engine *engine) {
    if (!engine) return;
    
    /* Destroy all remaining contexts */
    if (engine->contexts) {
        for (int i = 0; i < engine->context_count; i++) {
            if (engine->contexts[i]) {
                free(engine->contexts[i]->bytecode_cache);
                free(engine->contexts[i]->call_stack);
                free(engine->contexts[i]);
            }
        }
        free(engine->contexts);
    }
    
    /* Free heap */
    free(engine->heap);
    free(engine);
}

const char *zs_engine_version(void) {
    return ZS_VERSION_STRING;
}

/* ============================================================================
 * Context Management
 * ============================================================================ */

zs_context *zs_context_create(zs_engine *engine) {
    if (!engine) return NULL;
    
    zs_context *ctx = (zs_context *)malloc(sizeof(zs_context));
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(zs_context));
    ctx->engine = engine;
    
    /* Create global object */
    ctx->global = zs_object_create(ctx);
    
    /* Initialize built-ins */
    /* Object, Array, String, Number, Boolean, etc. */
    
    /* Add to engine contexts */
    engine->context_count++;
    engine->contexts = realloc(engine->contexts, 
                                sizeof(zs_context *) * engine->context_count);
    engine->contexts[engine->context_count - 1] = ctx;
    
    return ctx;
}

void zs_context_destroy(zs_context *ctx) {
    if (!ctx) return;
    
    /* Remove from engine context list */
    if (ctx->engine && ctx->engine->contexts) {
        for (int i = 0; i < ctx->engine->context_count; i++) {
            if (ctx->engine->contexts[i] == ctx) {
                ctx->engine->contexts[i] = NULL;
                break;
            }
        }
    }
    
    /* Run final GC */
    zs_gc_collect(ctx->engine);
    
    free(ctx->bytecode_cache);
    free(ctx->call_stack);
    free(ctx);
}

zs_object *zs_context_global(zs_context *ctx) {
    return ctx ? ctx->global : NULL;
}

void zs_context_set_userdata(zs_context *ctx, void *data) {
    if (ctx) ctx->user_data = data;
}

void *zs_context_get_userdata(zs_context *ctx) {
    return ctx ? ctx->user_data : NULL;
}

/* ============================================================================
 * Script Evaluation
 * ============================================================================ */

zs_value *zs_eval(zs_context *ctx, const char *code) {
    return zs_eval_file(ctx, code, "<eval>");
}

zs_value *zs_eval_file(zs_context *ctx, const char *code, const char *filename) {
    if (!ctx || !code) return zs_undefined(ctx);
    
    /* Compile to bytecode */
    zs_function *func = zs_compile(ctx, code, filename);
    if (!func || zs_has_exception(ctx)) {
        return zs_undefined(ctx);
    }
    
    /* Execute */
    return zs_execute(ctx, func);
}

zs_function *zs_compile(zs_context *ctx, const char *code, const char *filename) {
    if (!ctx || !code) return NULL;
    
    (void)filename;
    
    /* TODO: Implement full parser and bytecode compiler */
    /* For now, create placeholder function */
    
    zs_function *func = (zs_function *)malloc(sizeof(zs_function));
    if (!func) return NULL;
    
    memset(func, 0, sizeof(zs_function));
    func->base.type = ZS_TYPE_FUNCTION;
    func->name = "<script>";
    func->is_native = false;
    
    /* Store bytecode (placeholder) */
    size_t len = strlen(code) + 1;
    func->code.bytecode = malloc(len);
    memcpy(func->code.bytecode, code, len);
    
    return func;
}

zs_value *zs_execute(zs_context *ctx, zs_function *func) {
    if (!ctx || !func) return zs_undefined(ctx);
    
    /* TODO: Implement bytecode interpreter */
    /* For now, return undefined */
    
    (void)func;
    return zs_undefined(ctx);
}

bool zs_has_exception(zs_context *ctx) {
    return ctx && ctx->exception != NULL;
}

zs_value *zs_get_exception(zs_context *ctx) {
    if (!ctx) return NULL;
    zs_value *ex = ctx->exception;
    ctx->exception = NULL;
    return ex;
}

/* ============================================================================
 * Value Creation
 * ============================================================================ */

static zs_value *zs_value_alloc(zs_context *ctx, zs_value_type type) {
    zs_value *val = (zs_value *)malloc(sizeof(zs_value));
    if (!val) return NULL;
    
    memset(val, 0, sizeof(zs_value));
    val->type = type;
    
    /* Add to GC tracking */
    if (ctx && ctx->engine) {
        ctx->engine->gc_stats.objects_allocated++;
    }
    
    return val;
}

zs_value *zs_undefined(zs_context *ctx) {
    return zs_value_alloc(ctx, ZS_TYPE_UNDEFINED);
}

zs_value *zs_null(zs_context *ctx) {
    return zs_value_alloc(ctx, ZS_TYPE_NULL);
}

zs_value *zs_boolean(zs_context *ctx, bool value) {
    zs_value *val = zs_value_alloc(ctx, ZS_TYPE_BOOLEAN);
    if (val) val->as.boolean = value;
    return val;
}

zs_value *zs_number(zs_context *ctx, double value) {
    zs_value *val = zs_value_alloc(ctx, ZS_TYPE_NUMBER);
    if (val) val->as.number = value;
    return val;
}

zs_value *zs_integer(zs_context *ctx, int64_t value) {
    zs_value *val = zs_value_alloc(ctx, ZS_TYPE_NUMBER);
    if (val) val->as.number = (double)value;  /* Store as double for consistency */
    return val;
}

zs_value *zs_new_string(zs_context *ctx, const char *str) {
    return zs_string_len(ctx, str, str ? strlen(str) : 0);
}

zs_value *zs_string_len(zs_context *ctx, const char *str, size_t len) {
    zs_string_t *s = (zs_string_t *)malloc(sizeof(zs_string_t));
    if (!s) return NULL;
    
    memset(s, 0, sizeof(zs_string_t));
    s->base.type = ZS_TYPE_STRING;
    s->length = len;
    s->data = (char *)malloc(len + 1);
    if (s->data && str) {
        memcpy(s->data, str, len);
        s->data[len] = '\0';
    }
    
    /* Compute hash */
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (uint8_t)str[i];
    }
    s->hash = hash;
    
    if (ctx && ctx->engine) {
        ctx->engine->gc_stats.objects_allocated++;
    }
    
    return (zs_value *)s;
}

zs_object *zs_object_create(zs_context *ctx) {
    zs_object *obj = (zs_object *)malloc(sizeof(zs_object));
    if (!obj) return NULL;
    
    memset(obj, 0, sizeof(zs_object));
    obj->base.type = ZS_TYPE_OBJECT;
    
    if (ctx && ctx->engine) {
        ctx->engine->gc_stats.objects_allocated++;
    }
    
    return obj;
}

zs_value *zs_array_create(zs_context *ctx, int length) {
    (void)length;
    return (zs_value *)zs_object_create(ctx);
}

zs_value *zs_function_create(zs_context *ctx, zs_native_fn fn, const char *name, int arg_count) {
    zs_function *func = (zs_function *)malloc(sizeof(zs_function));
    if (!func) return NULL;
    
    memset(func, 0, sizeof(zs_function));
    func->base.type = ZS_TYPE_FUNCTION;
    func->name = name;
    func->arg_count = arg_count;
    func->is_native = true;
    func->code.native = fn;
    
    if (ctx && ctx->engine) {
        ctx->engine->gc_stats.objects_allocated++;
    }
    
    return (zs_value *)func;
}

/* ============================================================================
 * Value Inspection
 * ============================================================================ */

zs_value_type zs_typeof(zs_value *val) {
    return val ? val->type : ZS_TYPE_UNDEFINED;
}

bool zs_is_undefined(zs_value *val) { return !val || val->type == ZS_TYPE_UNDEFINED; }
bool zs_is_null(zs_value *val) { return val && val->type == ZS_TYPE_NULL; }
bool zs_is_boolean(zs_value *val) { return val && val->type == ZS_TYPE_BOOLEAN; }
bool zs_is_number(zs_value *val) { return val && val->type == ZS_TYPE_NUMBER; }
bool zs_is_string(zs_value *val) { return val && val->type == ZS_TYPE_STRING; }
bool zs_is_object(zs_value *val) { return val && val->type == ZS_TYPE_OBJECT; }
bool zs_is_function(zs_value *val) { return val && val->type == ZS_TYPE_FUNCTION; }
bool zs_is_array(zs_value *val) { return zs_is_object(val); /* TODO: Array tag */ }

bool zs_to_boolean(zs_value *val) {
    if (!val) return false;
    switch (val->type) {
        case ZS_TYPE_BOOLEAN: return val->as.boolean;
        case ZS_TYPE_NUMBER: return val->as.number != 0;
        case ZS_TYPE_STRING: return ((zs_string_t *)val)->length > 0;
        default: return val->type != ZS_TYPE_UNDEFINED && val->type != ZS_TYPE_NULL;
    }
}

double zs_to_number(zs_value *val) {
    if (!val) return 0;
    switch (val->type) {
        case ZS_TYPE_NUMBER: return val->as.number;
        case ZS_TYPE_BOOLEAN: return val->as.boolean ? 1 : 0;
        default: return 0;
    }
}

int64_t zs_to_integer(zs_value *val) {
    return (int64_t)zs_to_number(val);
}

const char *zs_to_string(zs_context *ctx, zs_value *val) {
    (void)ctx;
    if (!val) return "undefined";
    switch (val->type) {
        case ZS_TYPE_UNDEFINED: return "undefined";
        case ZS_TYPE_NULL: return "null";
        case ZS_TYPE_BOOLEAN: return val->as.boolean ? "true" : "false";
        case ZS_TYPE_STRING: return ((zs_string_t *)val)->data;
        default: return "[object]";
    }
}

/* ============================================================================
 * Garbage Collection
 * ============================================================================ */

void zs_gc_collect(zs_engine *engine) {
    if (!engine || engine->gc_mode == ZS_GC_DISABLED) return;
    
    /* TODO: Implement mark-sweep GC */
    engine->gc_stats.gc_runs++;
}

void zs_gc_set_mode(zs_engine *engine, zs_gc_mode mode) {
    if (engine) engine->gc_mode = mode;
}

void zs_gc_get_stats(zs_engine *engine, zs_gc_stats *stats) {
    if (engine && stats) {
        *stats = engine->gc_stats;
    }
}

/* ============================================================================
 * JIT Configuration
 * ============================================================================ */

void zs_jit_set_level(zs_engine *engine, zs_jit_level level) {
    if (engine) engine->jit_level = level;
}

zs_jit_level zs_jit_get_level(zs_engine *engine) {
    return engine ? engine->jit_level : ZS_JIT_OFF;
}

/* ============================================================================
 * Native API Binding
 * ============================================================================ */

void zs_register_global(zs_context *ctx, const char *name, zs_native_fn fn, int argc) {
    if (!ctx || !name || !fn) return;
    
    zs_value *func = zs_function_create(ctx, fn, name, argc);
    zs_set_property(ctx, ctx->global, name, func);
}

void zs_register_module(zs_context *ctx, const char *name, zs_object *exports) {
    if (!ctx || !name || !exports) return;
    zs_set_property(ctx, ctx->global, name, (zs_value *)exports);
}

void zs_register_class(zs_context *ctx, const char *name, zs_constructor_fn ctor) {
    (void)ctx; (void)name; (void)ctor;
    /* TODO: Implement class registration */
}

/* ============================================================================
 * Object/Property Operations
 * ============================================================================ */

zs_value *zs_get_property(zs_context *ctx, zs_object *obj, const char *name) {
    (void)ctx; (void)obj; (void)name;
    /* TODO: Implement property lookup with hidden class optimization */
    return NULL;
}

void zs_set_property(zs_context *ctx, zs_object *obj, const char *name, zs_value *val) {
    (void)ctx; (void)obj; (void)name; (void)val;
    /* TODO: Implement property storage with hidden class transition */
}

bool zs_has_property(zs_object *obj, const char *name) {
    (void)obj; (void)name;
    return false;
}

bool zs_delete_property(zs_object *obj, const char *name) {
    (void)obj; (void)name;
    return false;
}

zs_value *zs_get_index(zs_context *ctx, zs_value *arr, int index) {
    (void)ctx; (void)arr; (void)index;
    return NULL;
}

void zs_set_index(zs_context *ctx, zs_value *arr, int index, zs_value *val) {
    (void)ctx; (void)arr; (void)index; (void)val;
}

int zs_array_length(zs_value *arr) {
    (void)arr;
    return 0;
}

zs_value *zs_call(zs_context *ctx, zs_value *func, zs_value *this_val, 
                  int argc, zs_value **argv) {
    if (!ctx || !func || !zs_is_function(func)) return zs_undefined(ctx);
    
    zs_function *fn = (zs_function *)func;
    if (fn->is_native) {
        return fn->code.native(ctx, this_val, argc, argv);
    }
    
    /* TODO: Execute bytecode */
    return zs_undefined(ctx);
}

zs_value *zs_call_method(zs_context *ctx, zs_object *obj, const char *method,
                          int argc, zs_value **argv) {
    zs_value *func = zs_get_property(ctx, obj, method);
    return zs_call(ctx, func, (zs_value *)obj, argc, argv);
}
