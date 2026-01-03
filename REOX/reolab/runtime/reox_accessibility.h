/*
 * REOX Accessibility System
 * Comprehensive accessibility features for NeolyxOS UI
 * 
 * Features:
 * - VoiceOver/screen reader support
 * - Keyboard navigation
 * - Reduced motion support
 * - High contrast modes
 * - Font scaling
 * - Focus management
 */

#ifndef REOX_ACCESSIBILITY_H
#define REOX_ACCESSIBILITY_H

#include "reox_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Accessibility Roles
 * ============================================================================ */

typedef enum rx_a11y_role {
    RX_ROLE_NONE,
    RX_ROLE_BUTTON,
    RX_ROLE_CHECKBOX,
    RX_ROLE_RADIO,
    RX_ROLE_SWITCH,
    RX_ROLE_SLIDER,
    RX_ROLE_TEXTFIELD,
    RX_ROLE_LINK,
    RX_ROLE_IMAGE,
    RX_ROLE_HEADING,
    RX_ROLE_LIST,
    RX_ROLE_LISTITEM,
    RX_ROLE_TABLE,
    RX_ROLE_TABLECELL,
    RX_ROLE_MENU,
    RX_ROLE_MENUITEM,
    RX_ROLE_DIALOG,
    RX_ROLE_ALERT,
    RX_ROLE_PROGRESSBAR,
    RX_ROLE_TAB,
    RX_ROLE_TABPANEL,
    RX_ROLE_TOOLBAR,
    RX_ROLE_TOOLTIP,
    RX_ROLE_TREE,
    RX_ROLE_TREEITEM,
    RX_ROLE_SCROLLAREA,
    RX_ROLE_GROUP,
    RX_ROLE_REGION,
    RX_ROLE_APPLICATION,
    RX_ROLE_DOCUMENT,
    RX_ROLE_ARTICLE,
    RX_ROLE_BANNER,
    RX_ROLE_NAVIGATION,
    RX_ROLE_MAIN,
    RX_ROLE_COMPLEMENTARY,
    RX_ROLE_CONTENTINFO,
    RX_ROLE_SEARCH,
} rx_a11y_role;

/* ============================================================================
 * Accessibility Traits
 * ============================================================================ */

typedef enum rx_a11y_trait {
    RX_TRAIT_NONE            = 0,
    RX_TRAIT_BUTTON          = 1 << 0,
    RX_TRAIT_LINK            = 1 << 1,
    RX_TRAIT_IMAGE           = 1 << 2,
    RX_TRAIT_SELECTED        = 1 << 3,
    RX_TRAIT_PLAYS_SOUND     = 1 << 4,
    RX_TRAIT_KEYBOARD_KEY    = 1 << 5,
    RX_TRAIT_STATIC_TEXT     = 1 << 6,
    RX_TRAIT_SUMMARY         = 1 << 7,
    RX_TRAIT_DISABLED        = 1 << 8,
    RX_TRAIT_UPDATES_FREQ    = 1 << 9,  /* Updates frequently */
    RX_TRAIT_SEARCH_FIELD    = 1 << 10,
    RX_TRAIT_HEADER          = 1 << 11,
    RX_TRAIT_ADJUSTABLE      = 1 << 12,
    RX_TRAIT_MODAL           = 1 << 13,
    RX_TRAIT_CAUSES_PAGE_TURN = 1 << 14,
} rx_a11y_trait;

/* ============================================================================
 * Accessibility Actions
 * ============================================================================ */

typedef enum rx_a11y_action {
    RX_ACTION_ACTIVATE,       /* Primary action (tap/click) */
    RX_ACTION_ESCAPE,         /* Dismiss/cancel */
    RX_ACTION_INCREMENT,      /* Increase value */
    RX_ACTION_DECREMENT,      /* Decrease value */
    RX_ACTION_SCROLL_UP,
    RX_ACTION_SCROLL_DOWN,
    RX_ACTION_SCROLL_LEFT,
    RX_ACTION_SCROLL_RIGHT,
    RX_ACTION_DELETE,
    RX_ACTION_MAGIC_TAP,      /* Context action */
} rx_a11y_action;

typedef bool (*rx_a11y_action_handler)(rx_a11y_action action, void* user_data);

/* ============================================================================
 * Accessibility Element
 * ============================================================================ */

typedef struct rx_a11y_element {
    /* Identity */
    const char* identifier;
    rx_a11y_role role;
    uint32_t traits;
    
    /* Labels */
    const char* label;        /* Short description */
    const char* hint;         /* Usage instructions */
    const char* value;        /* Current value */
    
    /* State */
    bool is_accessible;
    bool is_hidden;
    bool is_modal;
    bool is_focusable;
    bool is_focused;
    
    /* Navigation */
    int navigation_order;     /* Tab order */
    struct rx_a11y_element* parent;
    struct rx_a11y_element** children;
    size_t child_count;
    
    /* Bounds for screen reader cursor */
    rx_rect accessible_frame;
    rx_point activation_point;
    
    /* Actions */
    rx_a11y_action_handler action_handler;
    void* action_data;
    uint32_t supported_actions;
    
    /* Custom actions */
    struct {
        const char* name;
        void (*handler)(void* data);
        void* data;
    }* custom_actions;
    size_t custom_action_count;
    
    /* Associated view */
    rx_view* view;
    
    struct rx_a11y_element* next;
} rx_a11y_element;

/* ============================================================================
 * Accessibility Element API
 * ============================================================================ */

extern rx_a11y_element* rx_a11y_element_create(rx_view* view);
extern void rx_a11y_set_label(rx_a11y_element* elem, const char* label);
extern void rx_a11y_set_hint(rx_a11y_element* elem, const char* hint);
extern void rx_a11y_set_value(rx_a11y_element* elem, const char* value);
extern void rx_a11y_set_role(rx_a11y_element* elem, rx_a11y_role role);
extern void rx_a11y_add_trait(rx_a11y_element* elem, rx_a11y_trait trait);
extern void rx_a11y_remove_trait(rx_a11y_element* elem, rx_a11y_trait trait);
extern void rx_a11y_set_hidden(rx_a11y_element* elem, bool hidden);
extern void rx_a11y_set_focusable(rx_a11y_element* elem, bool focusable);
extern void rx_a11y_set_action_handler(rx_a11y_element* elem, rx_a11y_action_handler handler, void* data);
extern void rx_a11y_add_custom_action(rx_a11y_element* elem, const char* name, void (*handler)(void* data), void* data);
extern void rx_a11y_element_destroy(rx_a11y_element* elem);

/* ============================================================================
 * Focus Management
 * ============================================================================ */

typedef struct rx_focus_system {
    rx_a11y_element* focused_element;
    rx_a11y_element* focus_ring;  /* All focusable elements */
    size_t focus_count;
    
    /* Focus trap (for modals) */
    rx_a11y_element* trap_container;
    
    /* Focus animation */
    bool show_focus_ring;
    rx_color focus_ring_color;
    float focus_ring_width;
} rx_focus_system;

extern rx_focus_system* rx_focus_system_create(void);
extern void rx_focus_set(rx_focus_system* sys, rx_a11y_element* elem);
extern void rx_focus_next(rx_focus_system* sys);
extern void rx_focus_previous(rx_focus_system* sys);
extern void rx_focus_first(rx_focus_system* sys);
extern void rx_focus_last(rx_focus_system* sys);
extern rx_a11y_element* rx_focus_current(rx_focus_system* sys);
extern void rx_focus_trap(rx_focus_system* sys, rx_a11y_element* container);
extern void rx_focus_release_trap(rx_focus_system* sys);
extern void rx_focus_system_destroy(rx_focus_system* sys);

/* Global focus system */
extern rx_focus_system* rx_focus_system_shared(void);

/* ============================================================================
 * Accessibility Preferences
 * ============================================================================ */

typedef struct rx_a11y_preferences {
    /* Motion */
    bool reduce_motion;
    bool reduce_transparency;
    bool auto_play_animations;
    
    /* Visual */
    bool high_contrast;
    bool invert_colors;
    bool grayscale;
    float bold_text_scale;
    float font_scale;         /* 1.0 = 100% */
    
    /* Hearing */
    bool show_captions;
    bool flash_screen;        /* Flash instead of sound */
    
    /* Touch */
    bool increase_contrast;
    float touch_delay;        /* Delay before registering touch */
    bool shake_to_undo;
    
    /* VoiceOver */
    bool voice_over_enabled;
    float speech_rate;        /* Words per minute */
    float voice_pitch;
    
    /* Color filters */
    bool color_filter_enabled;
    int color_filter_type;    /* Protanopia, Deuteranopia, etc. */
    float color_filter_intensity;
} rx_a11y_preferences;

extern rx_a11y_preferences* rx_a11y_preferences_get(void);
extern void rx_a11y_preferences_set(rx_a11y_preferences* prefs);
extern void rx_a11y_preferences_reset(void);

/* Preference observers */
typedef void (*rx_a11y_pref_callback)(rx_a11y_preferences* prefs, void* user_data);
extern void rx_a11y_observe_preferences(rx_a11y_pref_callback callback, void* user_data);

/* ============================================================================
 * Screen Reader Support
 * ============================================================================ */

typedef struct rx_screen_reader {
    bool enabled;
    rx_a11y_element* current_element;
    float speech_rate;
    
    /* Announcement queue */
    struct {
        const char* text;
        bool interrupt;
        bool is_important;
    }* announcements;
    size_t announcement_count;
} rx_screen_reader;

extern rx_screen_reader* rx_screen_reader_create(void);
extern void rx_screen_reader_announce(rx_screen_reader* sr, const char* text, bool interrupt);
extern void rx_screen_reader_announce_important(rx_screen_reader* sr, const char* text);
extern void rx_screen_reader_move_next(rx_screen_reader* sr);
extern void rx_screen_reader_move_previous(rx_screen_reader* sr);
extern void rx_screen_reader_activate(rx_screen_reader* sr);
extern void rx_screen_reader_escape(rx_screen_reader* sr);
extern void rx_screen_reader_read_all(rx_screen_reader* sr);
extern void rx_screen_reader_destroy(rx_screen_reader* sr);

/* Global announcements */
extern void rx_a11y_announce(const char* message);
extern void rx_a11y_announce_polite(const char* message);
extern void rx_a11y_announce_assertive(const char* message);

/* ============================================================================
 * Keyboard Navigation
 * ============================================================================ */

typedef struct rx_keyboard_shortcut {
    const char* key;
    uint32_t modifiers;       /* Ctrl, Alt, Shift, etc. */
    const char* description;
    void (*handler)(void* data);
    void* data;
} rx_keyboard_shortcut;

typedef struct rx_keyboard_nav {
    rx_keyboard_shortcut* shortcuts;
    size_t shortcut_count;
    bool arrow_navigation;    /* Enable arrow key navigation */
    bool tab_navigation;      /* Enable tab navigation */
    bool type_to_select;      /* Type to jump to item */
} rx_keyboard_nav;

extern rx_keyboard_nav* rx_keyboard_nav_create(void);
extern void rx_keyboard_add_shortcut(rx_keyboard_nav* nav, const char* key, uint32_t modifiers, const char* desc, void (*handler)(void* data), void* data);
extern void rx_keyboard_remove_shortcut(rx_keyboard_nav* nav, const char* key, uint32_t modifiers);
extern bool rx_keyboard_process_key(rx_keyboard_nav* nav, const char* key, uint32_t modifiers);
extern void rx_keyboard_nav_destroy(rx_keyboard_nav* nav);

/* Modifier constants */
#define RX_MOD_NONE    0
#define RX_MOD_SHIFT   (1 << 0)
#define RX_MOD_CTRL    (1 << 1)
#define RX_MOD_ALT     (1 << 2)
#define RX_MOD_META    (1 << 3)  /* Command on Mac */

/* ============================================================================
 * Reduced Motion Support
 * ============================================================================ */

/* Check if reduced motion is preferred */
extern bool rx_a11y_prefers_reduced_motion(void);

/* Get appropriate animation duration */
extern float rx_a11y_animation_duration(float normal_duration);

/* Get appropriate easing */
extern rx_easing rx_a11y_animation_easing(rx_easing normal_easing);

/* Skip or simplify animation */
extern bool rx_a11y_should_skip_animation(void);

/* ============================================================================
 * Live Regions
 * ============================================================================ */

typedef enum rx_live_behavior {
    RX_LIVE_OFF,              /* No announcements */
    RX_LIVE_POLITE,           /* Announce when idle */
    RX_LIVE_ASSERTIVE,        /* Announce immediately */
} rx_live_behavior;

typedef struct rx_live_region {
    rx_a11y_element* element;
    rx_live_behavior behavior;
    bool atomic;              /* Announce entire region */
    const char* relevant;     /* "additions", "removals", "text", "all" */
} rx_live_region;

extern rx_live_region* rx_live_region_create(rx_a11y_element* elem, rx_live_behavior behavior);
extern void rx_live_region_update(rx_live_region* region, const char* content);
extern void rx_live_region_destroy(rx_live_region* region);

/* ============================================================================
 * Accessibility Auditing
 * ============================================================================ */

typedef struct rx_a11y_issue {
    const char* type;         /* "error", "warning", "suggestion" */
    const char* message;
    const char* fix;
    rx_a11y_element* element;
    rx_rect highlight_rect;
} rx_a11y_issue;

typedef struct rx_a11y_audit_result {
    rx_a11y_issue* issues;
    size_t issue_count;
    int error_count;
    int warning_count;
    float score;              /* 0-100 */
} rx_a11y_audit_result;

extern rx_a11y_audit_result* rx_a11y_audit(rx_view* root);
extern void rx_a11y_audit_print(rx_a11y_audit_result* result);
extern void rx_a11y_audit_destroy(rx_a11y_audit_result* result);

/* Common checks */
extern bool rx_a11y_check_contrast(rx_color foreground, rx_color background);
extern bool rx_a11y_check_touch_target(rx_rect bounds);  /* Min 44x44 */
extern bool rx_a11y_check_label(rx_a11y_element* elem);

#ifdef __cplusplus
}
#endif

#endif /* REOX_ACCESSIBILITY_H */
