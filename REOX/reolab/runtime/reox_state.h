/*
 * REOX Reactive State System
 * Implements diff-based state management like React/SwiftUI
 * 
 * Features:
 * - Reactive state containers (State<T>)
 * - Automatic UI updates on state change
 * - Efficient diffing algorithm
 * - Computed/derived state
 * - State observers/subscriptions
 */

#ifndef REOX_STATE_H
#define REOX_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * State ID Generation
 * ============================================================================ */

static int64_t rx_state_id_counter = 0;

/* ============================================================================
 * State Types
 * ============================================================================ */

typedef enum {
    RX_STATE_INT,
    RX_STATE_FLOAT,
    RX_STATE_STRING,
    RX_STATE_BOOL,
    RX_STATE_OBJECT
} rx_state_type;

/* State change callback */
typedef void (*rx_state_callback)(int64_t state_id, void* old_value, void* new_value, void* user_data);

/* Observer entry */
typedef struct rx_observer {
    int64_t observer_id;
    rx_state_callback callback;
    void* user_data;
    struct rx_observer* next;
} rx_observer;

/* Base state container */
typedef struct rx_state {
    int64_t id;
    rx_state_type type;
    void* value;
    void* prev_value;           /* For diffing */
    bool dirty;                 /* Needs UI update */
    rx_observer* observers;     /* Subscribers */
    struct rx_state* next;      /* Global state list */
} rx_state;

/* Global state registry for diffing */
static rx_state* rx_state_registry = NULL;
static int64_t rx_observer_id_counter = 0;

/* ============================================================================
 * State Creation
 * ============================================================================ */

static rx_state* rx_state_create(rx_state_type type, void* initial_value, size_t size) {
    rx_state* s = (rx_state*)calloc(1, sizeof(rx_state));
    s->id = rx_state_id_counter++;
    s->type = type;
    s->value = malloc(size);
    s->prev_value = malloc(size);
    memcpy(s->value, initial_value, size);
    memcpy(s->prev_value, initial_value, size);
    s->dirty = false;
    s->observers = NULL;
    
    /* Add to registry */
    s->next = rx_state_registry;
    rx_state_registry = s;
    
    return s;
}

/* ============================================================================
 * State Accessors (Type-safe wrappers)
 * ============================================================================ */

typedef struct { int64_t id; } RxStateHandle;

/* Integer State */
RxStateHandle rx_state_int_create(int64_t initial) {
    int64_t val = initial;
    rx_state* s = rx_state_create(RX_STATE_INT, &val, sizeof(int64_t));
    return (RxStateHandle){ s->id };
}

int64_t rx_state_int_get(RxStateHandle handle) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) return *(int64_t*)s->value;
        s = s->next;
    }
    return 0;
}

void rx_state_int_set(RxStateHandle handle, int64_t value) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) {
            int64_t* old = (int64_t*)s->value;
            if (*old != value) {
                memcpy(s->prev_value, s->value, sizeof(int64_t));
                *old = value;
                s->dirty = true;
                
                /* Notify observers */
                rx_observer* obs = s->observers;
                while (obs) {
                    obs->callback(s->id, s->prev_value, s->value, obs->user_data);
                    obs = obs->next;
                }
            }
            return;
        }
        s = s->next;
    }
}

/* Float State */
RxStateHandle rx_state_float_create(double initial) {
    double val = initial;
    rx_state* s = rx_state_create(RX_STATE_FLOAT, &val, sizeof(double));
    return (RxStateHandle){ s->id };
}

double rx_state_float_get(RxStateHandle handle) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) return *(double*)s->value;
        s = s->next;
    }
    return 0.0;
}

void rx_state_float_set(RxStateHandle handle, double value) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) {
            double* old = (double*)s->value;
            if (*old != value) {
                memcpy(s->prev_value, s->value, sizeof(double));
                *old = value;
                s->dirty = true;
                
                rx_observer* obs = s->observers;
                while (obs) {
                    obs->callback(s->id, s->prev_value, s->value, obs->user_data);
                    obs = obs->next;
                }
            }
            return;
        }
        s = s->next;
    }
}

/* Bool State */
RxStateHandle rx_state_bool_create(bool initial) {
    bool val = initial;
    rx_state* s = rx_state_create(RX_STATE_BOOL, &val, sizeof(bool));
    return (RxStateHandle){ s->id };
}

bool rx_state_bool_get(RxStateHandle handle) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) return *(bool*)s->value;
        s = s->next;
    }
    return false;
}

void rx_state_bool_set(RxStateHandle handle, bool value) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) {
            bool* old = (bool*)s->value;
            if (*old != value) {
                memcpy(s->prev_value, s->value, sizeof(bool));
                *old = value;
                s->dirty = true;
                
                rx_observer* obs = s->observers;
                while (obs) {
                    obs->callback(s->id, s->prev_value, s->value, obs->user_data);
                    obs = obs->next;
                }
            }
            return;
        }
        s = s->next;
    }
}

/* String State */
typedef struct { char data[256]; } RxString;

RxStateHandle rx_state_string_create(const char* initial) {
    RxString val = {0};
    if (initial) strncpy(val.data, initial, 255);
    rx_state* s = rx_state_create(RX_STATE_STRING, &val, sizeof(RxString));
    return (RxStateHandle){ s->id };
}

const char* rx_state_string_get(RxStateHandle handle) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) return ((RxString*)s->value)->data;
        s = s->next;
    }
    return "";
}

void rx_state_string_set(RxStateHandle handle, const char* value) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) {
            RxString* old = (RxString*)s->value;
            if (strcmp(old->data, value ? value : "") != 0) {
                memcpy(s->prev_value, s->value, sizeof(RxString));
                strncpy(old->data, value ? value : "", 255);
                s->dirty = true;
                
                rx_observer* obs = s->observers;
                while (obs) {
                    obs->callback(s->id, s->prev_value, s->value, obs->user_data);
                    obs = obs->next;
                }
            }
            return;
        }
        s = s->next;
    }
}

/* ============================================================================
 * State Observers (Subscriptions)
 * ============================================================================ */

int64_t rx_state_observe(RxStateHandle handle, rx_state_callback callback, void* user_data) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) {
            rx_observer* obs = (rx_observer*)malloc(sizeof(rx_observer));
            obs->observer_id = rx_observer_id_counter++;
            obs->callback = callback;
            obs->user_data = user_data;
            obs->next = s->observers;
            s->observers = obs;
            return obs->observer_id;
        }
        s = s->next;
    }
    return -1;
}

void rx_state_unobserve(RxStateHandle handle, int64_t observer_id) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->id == handle.id) {
            rx_observer** pp = &s->observers;
            while (*pp) {
                if ((*pp)->observer_id == observer_id) {
                    rx_observer* to_free = *pp;
                    *pp = (*pp)->next;
                    free(to_free);
                    return;
                }
                pp = &(*pp)->next;
            }
            return;
        }
        s = s->next;
    }
}

/* ============================================================================
 * Diff Engine
 * ============================================================================ */

typedef struct {
    int64_t state_id;
    rx_state_type type;
    void* old_value;
    void* new_value;
} rx_state_diff;

typedef struct {
    rx_state_diff* diffs;
    int count;
    int capacity;
} rx_diff_batch;

/* Collect all dirty states */
rx_diff_batch rx_collect_diffs(void) {
    rx_diff_batch batch = { NULL, 0, 16 };
    batch.diffs = (rx_state_diff*)malloc(sizeof(rx_state_diff) * batch.capacity);
    
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->dirty) {
            if (batch.count >= batch.capacity) {
                batch.capacity *= 2;
                batch.diffs = (rx_state_diff*)realloc(batch.diffs, sizeof(rx_state_diff) * batch.capacity);
            }
            
            batch.diffs[batch.count].state_id = s->id;
            batch.diffs[batch.count].type = s->type;
            batch.diffs[batch.count].old_value = s->prev_value;
            batch.diffs[batch.count].new_value = s->value;
            batch.count++;
        }
        s = s->next;
    }
    
    return batch;
}

/* Clear dirty flags after applying diffs */
void rx_clear_dirty(void) {
    rx_state* s = rx_state_registry;
    while (s) {
        if (s->dirty) {
            s->dirty = false;
            /* Update prev_value to current */
            size_t size = 0;
            switch (s->type) {
                case RX_STATE_INT: size = sizeof(int64_t); break;
                case RX_STATE_FLOAT: size = sizeof(double); break;
                case RX_STATE_BOOL: size = sizeof(bool); break;
                case RX_STATE_STRING: size = sizeof(RxString); break;
                default: break;
            }
            if (size > 0) memcpy(s->prev_value, s->value, size);
        }
        s = s->next;
    }
}

/* Print diff for debugging */
void rx_print_diff(rx_state_diff* diff) {
    printf("[DIFF] State #%ld: ", diff->state_id);
    switch (diff->type) {
        case RX_STATE_INT:
            printf("%ld -> %ld\n", *(int64_t*)diff->old_value, *(int64_t*)diff->new_value);
            break;
        case RX_STATE_FLOAT:
            printf("%.2f -> %.2f\n", *(double*)diff->old_value, *(double*)diff->new_value);
            break;
        case RX_STATE_BOOL:
            printf("%s -> %s\n", 
                *(bool*)diff->old_value ? "true" : "false",
                *(bool*)diff->new_value ? "true" : "false");
            break;
        case RX_STATE_STRING:
            printf("'%s' -> '%s'\n", 
                ((RxString*)diff->old_value)->data,
                ((RxString*)diff->new_value)->data);
            break;
        default:
            printf("(unknown type)\n");
    }
}

/* ============================================================================
 * Computed State (Derived values)
 * ============================================================================ */

typedef double (*rx_compute_fn)(void* user_data);

typedef struct rx_computed {
    int64_t id;
    RxStateHandle* dependencies;
    int dep_count;
    rx_compute_fn compute;
    void* user_data;
    double cached_value;
    bool needs_recompute;
    struct rx_computed* next;
} rx_computed;

static rx_computed* rx_computed_registry = NULL;

RxStateHandle rx_computed_create(RxStateHandle* deps, int dep_count, rx_compute_fn compute, void* user_data) {
    rx_computed* c = (rx_computed*)calloc(1, sizeof(rx_computed));
    c->id = rx_state_id_counter++;
    c->dep_count = dep_count;
    c->dependencies = (RxStateHandle*)malloc(sizeof(RxStateHandle) * dep_count);
    memcpy(c->dependencies, deps, sizeof(RxStateHandle) * dep_count);
    c->compute = compute;
    c->user_data = user_data;
    c->cached_value = compute(user_data);
    c->needs_recompute = false;
    
    c->next = rx_computed_registry;
    rx_computed_registry = c;
    
    return (RxStateHandle){ c->id };
}

double rx_computed_get(RxStateHandle handle) {
    rx_computed* c = rx_computed_registry;
    while (c) {
        if (c->id == handle.id) {
            if (c->needs_recompute) {
                c->cached_value = c->compute(c->user_data);
                c->needs_recompute = false;
            }
            return c->cached_value;
        }
        c = c->next;
    }
    return 0.0;
}

/* ============================================================================
 * Effect System (Side effects on state change)
 * ============================================================================ */

typedef void (*rx_effect_fn)(void* user_data);

typedef struct rx_effect {
    int64_t id;
    RxStateHandle* dependencies;
    int dep_count;
    rx_effect_fn effect;
    void* user_data;
    struct rx_effect* next;
} rx_effect;

static rx_effect* rx_effect_registry = NULL;

int64_t rx_effect_create(RxStateHandle* deps, int dep_count, rx_effect_fn effect, void* user_data) {
    rx_effect* e = (rx_effect*)calloc(1, sizeof(rx_effect));
    e->id = rx_state_id_counter++;
    e->dep_count = dep_count;
    e->dependencies = (RxStateHandle*)malloc(sizeof(RxStateHandle) * dep_count);
    memcpy(e->dependencies, deps, sizeof(RxStateHandle) * dep_count);
    e->effect = effect;
    e->user_data = user_data;
    
    e->next = rx_effect_registry;
    rx_effect_registry = e;
    
    /* Run effect immediately */
    effect(user_data);
    
    return e->id;
}

/* Run effects for dirty dependencies */
void rx_run_effects(void) {
    rx_effect* e = rx_effect_registry;
    while (e) {
        bool should_run = false;
        for (int i = 0; i < e->dep_count; i++) {
            rx_state* s = rx_state_registry;
            while (s) {
                if (s->id == e->dependencies[i].id && s->dirty) {
                    should_run = true;
                    break;
                }
                s = s->next;
            }
            if (should_run) break;
        }
        if (should_run) {
            e->effect(e->user_data);
        }
        e = e->next;
    }
}

/* ============================================================================
 * Batch Update (Transaction)
 * ============================================================================ */

static bool rx_batch_mode = false;

void rx_batch_begin(void) {
    rx_batch_mode = true;
}

void rx_batch_commit(void) {
    rx_batch_mode = false;
    
    /* Collect and apply diffs */
    rx_diff_batch batch = rx_collect_diffs();
    
    if (batch.count > 0) {
        printf("[STATE] Committing %d changes:\n", batch.count);
        for (int i = 0; i < batch.count; i++) {
            rx_print_diff(&batch.diffs[i]);
        }
        
        /* Run effects */
        rx_run_effects();
        
        /* Clear dirty flags */
        rx_clear_dirty();
    }
    
    free(batch.diffs);
}

#endif /* REOX_STATE_H */
