/*
 * REOX Forms and Data Binding
 * 
 * Form validation, data binding, and state management.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef REOX_FORMS_H
#define REOX_FORMS_H

#include "reox_ui.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Form Validation
 * ============================================================================ */

typedef enum rx_validation_type {
    RX_VALID_REQUIRED,        /* Field must not be empty */
    RX_VALID_MIN_LENGTH,      /* Minimum string length */
    RX_VALID_MAX_LENGTH,      /* Maximum string length */
    RX_VALID_PATTERN,         /* Regex pattern */
    RX_VALID_EMAIL,           /* Email format */
    RX_VALID_URL,             /* URL format */
    RX_VALID_NUMBER,          /* Numeric only */
    RX_VALID_INTEGER,         /* Integer only */
    RX_VALID_RANGE,           /* Numeric range */
    RX_VALID_CUSTOM,          /* Custom validator function */
} rx_validation_type;

typedef bool (*rx_custom_validator)(const char *value, void *user_data);

typedef struct rx_validation_rule {
    rx_validation_type type;
    union {
        int length;           /* For MIN_LENGTH, MAX_LENGTH */
        float range[2];       /* For RANGE: [min, max] */
        const char *pattern;  /* For PATTERN */
        struct {
            rx_custom_validator fn;
            void *data;
        } custom;
    } params;
    const char *error_message;
} rx_validation_rule;

typedef struct rx_validation_result {
    bool valid;
    const char *error_message;
    rx_validation_rule *failed_rule;
} rx_validation_result;

/* Validate a value against rules */
rx_validation_result rx_validate(const char *value, rx_validation_rule *rules, int rule_count);

/* Common validation rules (pre-built) */
extern rx_validation_rule rx_rule_required(const char *error_msg);
extern rx_validation_rule rx_rule_min_length(int min, const char *error_msg);
extern rx_validation_rule rx_rule_max_length(int max, const char *error_msg);
extern rx_validation_rule rx_rule_email(const char *error_msg);
extern rx_validation_rule rx_rule_url(const char *error_msg);
extern rx_validation_rule rx_rule_number(const char *error_msg);
extern rx_validation_rule rx_rule_range(float min, float max, const char *error_msg);

/* ============================================================================
 * Form Field
 * ============================================================================ */

typedef enum rx_field_type {
    RX_FIELD_TEXT,
    RX_FIELD_PASSWORD,
    RX_FIELD_EMAIL,
    RX_FIELD_NUMBER,
    RX_FIELD_TEXTAREA,
    RX_FIELD_SELECT,
    RX_FIELD_CHECKBOX,
    RX_FIELD_RADIO,
    RX_FIELD_SWITCH,
    RX_FIELD_SLIDER,
    RX_FIELD_DATE,
    RX_FIELD_TIME,
    RX_FIELD_FILE,
} rx_field_type;

typedef struct rx_form_field {
    const char *name;         /* Field identifier */
    const char *label;        /* Display label */
    rx_field_type type;
    char *value;              /* Current value as string */
    const char *placeholder;
    const char *helper_text;
    rx_validation_rule *rules;
    int rule_count;
    rx_validation_result validation;
    bool touched;             /* User has interacted */
    bool dirty;               /* Value has changed */
    rx_view *view;            /* Associated view */
} rx_form_field;

/* ============================================================================
 * Form
 * ============================================================================ */

typedef void (*rx_form_submit_callback)(rx_form_field *fields, int count, void *user_data);

typedef struct rx_form {
    rx_form_field *fields;
    int field_count;
    bool valid;               /* All fields valid */
    bool validate_on_change;  /* Validate as user types */
    bool validate_on_blur;    /* Validate when field loses focus */
    rx_form_submit_callback on_submit;
    void *submit_data;
} rx_form;

/* Form lifecycle */
extern rx_form *rx_form_new(void);
extern void rx_form_free(rx_form *form);

/* Field management */
extern rx_form_field *rx_form_add_field(rx_form *form, const char *name, const char *label, rx_field_type type);
extern rx_form_field *rx_form_get_field(rx_form *form, const char *name);
extern void rx_form_set_value(rx_form *form, const char *name, const char *value);
extern const char *rx_form_get_value(rx_form *form, const char *name);

/* Validation */
extern bool rx_form_validate(rx_form *form);
extern bool rx_form_validate_field(rx_form *form, const char *name);
extern void rx_form_clear_errors(rx_form *form);

/* Actions */
extern void rx_form_submit(rx_form *form);
extern void rx_form_reset(rx_form *form);

/* ============================================================================
 * Data Binding
 * ============================================================================ */

typedef enum rx_binding_mode {
    RX_BIND_ONE_WAY,          /* Source -> View */
    RX_BIND_TWO_WAY,          /* Source <-> View */
    RX_BIND_ONE_TIME,         /* Initial only */
} rx_binding_mode;

typedef void (*rx_binding_transform)(void *source, void *dest, void *user_data);

typedef struct rx_binding {
    void *source;             /* Source data pointer */
    size_t source_size;       /* Size of source data */
    rx_view *target;          /* Target view */
    const char *property;     /* Property to bind to */
    rx_binding_mode mode;
    rx_binding_transform transform;
    void *transform_data;
    struct rx_binding *next;  /* Linked list */
} rx_binding;

/* Binding manager */
typedef struct rx_binding_context {
    rx_binding *bindings;
    int binding_count;
} rx_binding_context;

/* Create binding context */
extern rx_binding_context *rx_binding_context_new(void);
extern void rx_binding_context_free(rx_binding_context *ctx);

/* Create bindings */
extern rx_binding *rx_bind(rx_binding_context *ctx, void *source, size_t size, 
                           rx_view *target, const char *property, rx_binding_mode mode);
extern rx_binding *rx_bind_transform(rx_binding_context *ctx, void *source, size_t size,
                                      rx_view *target, const char *property,
                                      rx_binding_transform fn, void *data);

/* Update bindings */
extern void rx_binding_notify(rx_binding_context *ctx, void *source);
extern void rx_binding_sync_all(rx_binding_context *ctx);

/* ============================================================================
 * Observable State
 * ============================================================================ */

typedef void (*rx_state_observer)(void *state, void *user_data);

typedef struct rx_observable {
    void *value;
    size_t size;
    rx_state_observer *observers;
    void **observer_data;
    int observer_count;
    int observer_capacity;
} rx_observable;

/* Create observable */
extern rx_observable *rx_observable_new(void *initial_value, size_t size);
extern void rx_observable_free(rx_observable *obs);

/* Get/set value */
extern void *rx_observable_get(rx_observable *obs);
extern void rx_observable_set(rx_observable *obs, void *value);

/* Subscribe to changes */
extern void rx_observable_subscribe(rx_observable *obs, rx_state_observer observer, void *user_data);
extern void rx_observable_unsubscribe(rx_observable *obs, rx_state_observer observer);

/* ============================================================================
 * Form View Helper
 * ============================================================================ */

typedef struct rx_form_view {
    rx_view base;
    rx_form *form;
    rx_button_view *submit_button;
    rx_button_view *reset_button;
    float field_spacing;
    bool show_labels;
    bool show_errors;
    bool inline_layout;       /* Fields side by side vs stacked */
} rx_form_view;

extern rx_form_view *form_view_new(rx_form *form);
extern void form_view_set_submit_label(rx_form_view *view, const char *label);
extern void form_view_set_reset_label(rx_form_view *view, const char *label);

#ifdef __cplusplus
}
#endif

#endif /* REOX_FORMS_H */
