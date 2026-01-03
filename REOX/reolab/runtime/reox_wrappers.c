/*
 * REOX Language Wrappers
 * Bridge between generated REOX C code and the UI runtime  
 * Uses rx_ prefix for internal calls to avoid header conflicts
 */

#define RX_NO_INLINE_HELPERS  /* Don't include inline helpers from header */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations for internal UI types */
typedef struct rx_view rx_view;
typedef struct rx_app rx_app;
typedef struct rx_window rx_window;
typedef struct rx_text_view rx_text_view;
typedef struct rx_button_view rx_button_view;

/* Internal UI functions (defined in reox_ui.c) */
extern rx_view* view_new_internal(int kind);
extern rx_text_view* text_view_new_internal(const char* text);
extern rx_button_view* button_view_new_internal(const char* label);
extern void view_layout_internal(rx_view* view, float aw, float ah);
extern void view_render_internal(rx_view* view, void* ctx);

/* ============================================================================
 * POD Types (matching REOX generated structs)
 * ============================================================================ */

typedef struct { int64_t r, g, b, a; } Color;
typedef struct { double top, right, bottom, left; } EdgeInsets;
typedef struct { double width; Color color; int64_t style; } Border;
typedef struct { double offset_x, offset_y, blur; Color color; } Shadow;  
typedef struct { int64_t handle; } View;
typedef struct { int64_t handle; } App;
typedef struct { int64_t handle; } Window;

/* Global handle tables */
#define MAX_VIEWS 1024
#define MAX_APPS 16
#define MAX_WINDOWS 64

static void* g_views[MAX_VIEWS];
static int g_view_count = 0;
static void* g_apps[MAX_APPS];
static int g_app_count = 0;

typedef struct {
    const char* title;
    float width;
    float height;
    void* root_view;
} SimpleWindow;

static SimpleWindow g_windows[MAX_WINDOWS];
static int g_win_count = 0;

/* Simple view structure for demo */
typedef struct SimpleView {
    int kind;  /* 0=box, 1=text, 2=button */
    char label[256];
    struct SimpleView* children[64];
    int child_count;
    float gap;
    int direction;  /* 0=vertical, 1=horizontal */
    struct {
        uint8_t r, g, b, a;
    } background;
    float padding;
    float corner_radius;
    struct { float x, y, blur; } shadow;
    struct { float width; } border;
} SimpleView;

static View wrap_view(SimpleView* v) { 
    g_views[g_view_count] = v; 
    return (View){ g_view_count++ }; 
}

static SimpleView* unwrap_view(View v) { 
    return (v.handle >= 0 && v.handle < g_view_count) ? (SimpleView*)g_views[v.handle] : NULL; 
}

static SimpleView* create_view(int kind, const char* label) {
    SimpleView* v = (SimpleView*)calloc(1, sizeof(SimpleView));
    v->kind = kind;
    if (label) strncpy(v->label, label, 255);
    return v;
}

/* ============================================================================
 * Color Functions
 * ============================================================================ */

Color color_background(void) { return (Color){28, 28, 30, 255}; }
Color color_surface(void) { return (Color){44, 44, 46, 255}; }
Color color_text(void) { return (Color){255, 255, 255, 255}; }
Color color_primary(void) { return (Color){0, 122, 255, 255}; }
Color color_accent(void) { return (Color){255, 69, 58, 255}; }

Color color_rgba(int64_t r, int64_t g, int64_t b, int64_t a) {
    return (Color){r, g, b, a};
}

Color color_lighten(Color c, double amount) {
    return (Color){
        c.r + (int64_t)((255 - c.r) * amount),
        c.g + (int64_t)((255 - c.g) * amount),
        c.b + (int64_t)((255 - c.b) * amount),
        c.a
    };
}

Color color_darken(Color c, double amount) {
    return (Color){
        (int64_t)(c.r * (1 - amount)),
        (int64_t)(c.g * (1 - amount)),
        (int64_t)(c.b * (1 - amount)),
        c.a
    };
}

/* ============================================================================
 * Geometry Functions
 * ============================================================================ */

EdgeInsets insets_all(double value) {
    return (EdgeInsets){value, value, value, value};
}

EdgeInsets insets_symmetric(double v, double h) {
    return (EdgeInsets){v, h, v, h};
}

Border border(double width, Color c) {
    return (Border){width, c, 1};
}

Shadow shadow(double x, double y, double blur, Color c) {
    return (Shadow){x, y, blur, c};
}

/* ============================================================================
 * Layout Containers
 * ============================================================================ */

View vstack(double gap) {
    SimpleView* v = create_view(0, "VStack");
    v->direction = 0;
    v->gap = (float)gap;
    printf("[UI] VStack(gap: %.0f)\n", gap);
    return wrap_view(v);
}

View hstack(double gap) {
    SimpleView* v = create_view(0, "HStack");
    v->direction = 1;
    v->gap = (float)gap;
    printf("[UI] HStack(gap: %.0f)\n", gap);
    return wrap_view(v);
}

/* ============================================================================
 * View Constructors
 * ============================================================================ */

View text_view(const char* text) {
    SimpleView* v = create_view(1, text);
    printf("[UI]   Text: '%s'\n", text);
    return wrap_view(v);
}

View button_view(const char* label) {
    SimpleView* v = create_view(2, label);
    printf("[UI]   Button: '%s'\n", label);
    return wrap_view(v);
}

/* ============================================================================
 * View Modifiers
 * ============================================================================ */

void view_set_background(View v, Color c) {
    SimpleView* vw = unwrap_view(v);
    if (vw) {
        vw->background.r = (uint8_t)c.r;
        vw->background.g = (uint8_t)c.g;
        vw->background.b = (uint8_t)c.b;
        vw->background.a = (uint8_t)c.a;
    }
    printf("[UI]     background: rgba(%ld,%ld,%ld,%ld)\n", c.r, c.g, c.b, c.a);
}

void view_set_padding(View v, EdgeInsets p) {
    SimpleView* vw = unwrap_view(v);
    if (vw) vw->padding = (float)p.top;
    printf("[UI]     padding: %.0fpx\n", p.top);
}

void view_set_corner_radius(View v, double r) {
    SimpleView* vw = unwrap_view(v);
    if (vw) vw->corner_radius = (float)r;
    printf("[UI]     corner-radius: %.0fpx\n", r);
}

void view_set_shadow(View v, Shadow s) {
    SimpleView* vw = unwrap_view(v);
    if (vw) {
        vw->shadow.x = (float)s.offset_x;
        vw->shadow.y = (float)s.offset_y;
        vw->shadow.blur = (float)s.blur;
    }
    printf("[UI]     shadow: %.0f %.0f %.0f\n", s.offset_x, s.offset_y, s.blur);
}

void view_set_border(View v, Border b) {
    SimpleView* vw = unwrap_view(v);
    if (vw) vw->border.width = (float)b.width;
    printf("[UI]     border: %.0fpx solid\n", b.width);
}

void view_add_child(View parent, View child) {
    SimpleView* p = unwrap_view(parent);
    SimpleView* c = unwrap_view(child);
    if (p && c && p->child_count < 64) {
        p->children[p->child_count++] = c;
    }
}

/* ============================================================================
 * Text Modifiers
 * ============================================================================ */

void text_set_font_size(View v, double size) {
    (void)v; (void)size;  /* Store if needed */
}

void text_set_font_weight(View v, int64_t weight) {
    (void)v; (void)weight;
}

void text_set_color(View v, Color c) {
    (void)v; (void)c;
}

/* ============================================================================
 * Button Modifiers
 * ============================================================================ */

void button_set_style(View v, Color normal, Color hover, Color pressed) {
    (void)v; (void)normal; (void)hover; (void)pressed;
}

/* ============================================================================
 * App & Window
 * ============================================================================ */

App app_new(const char* name) {
    printf("[App] Created: %s\n", name);
    return (App){g_app_count++};
}

Window app_create_window(App a, const char* title, int64_t w, int64_t h) {
    (void)a;
    g_windows[g_win_count].title = title;
    g_windows[g_win_count].width = (float)w;
    g_windows[g_win_count].height = (float)h;
    printf("[Window] Created: %s (%ldx%ld)\n", title, w, h);
    return (Window){g_win_count++};
}

void app_run(App a) {
    (void)a;
    printf("[App] Running...\n");
    printf("[App] UI layout complete\n");
}

void window_set_root(Window win, View v) {
    if (win.handle >= 0 && win.handle < g_win_count) {
        g_windows[win.handle].root_view = unwrap_view(v);
        printf("[Window] Root view set\n");
    }
}

/* ============================================================================
 * State Management
 * ============================================================================ */

typedef struct { int64_t value; int64_t id; } StateInt;
typedef struct { double value; int64_t id; } StateFloat;
typedef struct { const char* value; int64_t id; } StateString;
typedef struct { int64_t value; int64_t id; } StateBool;

static int64_t g_state_counter = 0;

StateInt state_int(int64_t initial) {
    return (StateInt){ initial, g_state_counter++ };
}

StateFloat state_float(double initial) {
    return (StateFloat){ initial, g_state_counter++ };
}

StateString state_string(const char* initial) {
    return (StateString){ initial, g_state_counter++ };
}

StateBool state_bool(int64_t initial) {
    return (StateBool){ initial, g_state_counter++ };
}

int64_t state_get_int(StateInt s) { return s.value; }
void state_set_int(StateInt* s, int64_t value) { if (s) s->value = value; }
double state_get_float(StateFloat s) { return s.value; }
void state_set_float(StateFloat* s, double value) { if (s) s->value = value; }
const char* state_get_string(StateString s) { return s.value; }
void state_set_string(StateString* s, const char* value) { if (s) s->value = value; }
int64_t state_get_bool(StateBool s) { return s.value; }
void state_set_bool(StateBool* s, int64_t value) { if (s) s->value = value; }

/* ============================================================================
 * Extended UI Components
 * ============================================================================ */

View zstack(void) {
    SimpleView* v = create_view(0, "ZStack");
    v->direction = 2; /* stack */
    printf("[UI] ZStack()\n");
    return wrap_view(v);
}

View scroll_view(void) {
    SimpleView* v = create_view(0, "ScrollView");
    printf("[UI] ScrollView()\n");
    return wrap_view(v);
}

View list_view(void) {
    SimpleView* v = create_view(0, "ListView");
    printf("[UI] ListView()\n");
    return wrap_view(v);
}

View grid_view(int64_t cols, double gap) {
    SimpleView* v = create_view(0, "GridView");
    v->gap = (float)gap;
    printf("[UI] GridView(cols: %ld, gap: %.0f)\n", cols, gap);
    return wrap_view(v);
}

View spacer(void) {
    SimpleView* v = create_view(0, "Spacer");
    printf("[UI] Spacer()\n");
    return wrap_view(v);
}

View divider(void) {
    SimpleView* v = create_view(0, "Divider");
    printf("[UI] Divider()\n");
    return wrap_view(v);
}

View icon_view(const char* name) {
    SimpleView* v = create_view(1, name);
    printf("[UI]   Icon: '%s'\n", name);
    return wrap_view(v);
}

View text_field(const char* placeholder) {
    SimpleView* v = create_view(3, placeholder);
    printf("[UI]   TextField: '%s'\n", placeholder);
    return wrap_view(v);
}

View text_area(const char* placeholder) {
    SimpleView* v = create_view(4, placeholder);
    printf("[UI]   TextArea: '%s'\n", placeholder);
    return wrap_view(v);
}

View checkbox_view(const char* label, int64_t checked) {
    SimpleView* v = create_view(5, label);
    printf("[UI]   Checkbox: '%s' (%s)\n", label, checked ? "checked" : "unchecked");
    return wrap_view(v);
}

View toggle_view(const char* label, int64_t on) {
    SimpleView* v = create_view(6, label);
    printf("[UI]   Toggle: '%s' (%s)\n", label, on ? "on" : "off");
    return wrap_view(v);
}

View slider_view(double min_val, double max_val, double value) {
    SimpleView* v = create_view(7, "Slider");
    printf("[UI]   Slider: %.0f-%.0f (value: %.0f)\n", min_val, max_val, value);
    return wrap_view(v);
}

View picker_view(const char* options) {
    SimpleView* v = create_view(8, options);
    printf("[UI]   Picker: '%s'\n", options);
    return wrap_view(v);
}

View date_picker(void) {
    SimpleView* v = create_view(9, "DatePicker");
    printf("[UI] DatePicker()\n");
    return wrap_view(v);
}

View color_picker(void) {
    SimpleView* v = create_view(10, "ColorPicker");
    printf("[UI] ColorPicker()\n");
    return wrap_view(v);
}

View image_view(const char* source) {
    SimpleView* v = create_view(11, source);
    printf("[UI]   Image: '%s'\n", source);
    return wrap_view(v);
}

/* Extended View Modifiers */
void view_set_size(View v, double w, double h) {
    (void)v; 
    printf("[UI]     size: %.0f x %.0f\n", w, h);
}

void view_set_min_size(View v, double w, double h) {
    (void)v;
    printf("[UI]     min-size: %.0f x %.0f\n", w, h);
}

void view_set_max_size(View v, double w, double h) {
    (void)v;
    printf("[UI]     max-size: %.0f x %.0f\n", w, h);
}

void view_set_opacity(View v, double alpha) {
    (void)v;
    printf("[UI]     opacity: %.2f\n", alpha);
}

void view_set_hidden(View v, int64_t hidden) {
    (void)v;
    printf("[UI]     hidden: %s\n", hidden ? "true" : "false");
}

void view_set_enabled(View v, int64_t enabled) {
    (void)v;
    printf("[UI]     enabled: %s\n", enabled ? "true" : "false");
}

void text_set_text(View v, const char* text) {
    SimpleView* sv = unwrap_view(v);
    if (sv) strncpy(sv->label, text, 255);
}

void text_set_align(View v, int64_t align) {
    (void)v;
    const char* aligns[] = {"left", "center", "right"};
    printf("[UI]     text-align: %s\n", align < 3 ? aligns[align] : "left");
}

void button_set_on_click(View v, int64_t callback_id) {
    (void)v; (void)callback_id;
}

void button_set_disabled(View v, int64_t disabled) {
    (void)v;
    printf("[UI]     button-disabled: %s\n", disabled ? "true" : "false");
}

void input_set_value(View v, const char* value) {
    SimpleView* sv = unwrap_view(v);
    if (sv) strncpy(sv->label, value, 255);
}

const char* input_get_value(View v) {
    SimpleView* sv = unwrap_view(v);
    return sv ? sv->label : "";
}

void input_set_on_change(View v, int64_t callback_id) {
    (void)v; (void)callback_id;
}

void input_set_password(View v, int64_t is_password) {
    (void)v;
    printf("[UI]     password: %s\n", is_password ? "true" : "false");
}

/* Extended Color Functions */
Color color_secondary(void) { return (Color){88, 86, 214, 255}; }
Color color_success(void) { return (Color){52, 199, 89, 255}; }
Color color_warning(void) { return (Color){255, 204, 0, 255}; }
Color color_error(void) { return (Color){255, 59, 48, 255}; }

Color color_hex(int64_t hex) {
    return (Color){
        (hex >> 16) & 0xFF,
        (hex >> 8) & 0xFF,
        hex & 0xFF,
        255
    };
}

Color color_blend(Color a, Color b, double t) {
    return (Color){
        (int64_t)(a.r + (b.r - a.r) * t),
        (int64_t)(a.g + (b.g - a.g) * t),
        (int64_t)(a.b + (b.b - a.b) * t),
        (int64_t)(a.a + (b.a - a.a) * t)
    };
}

/* Window Extensions */
void window_set_title(Window win, const char* title) {
    if (win.handle >= 0 && win.handle < g_win_count) {
        g_windows[win.handle].title = title;
        printf("[Window] Title: %s\n", title);
    }
}

void window_set_size(Window win, int64_t w, int64_t h) {
    if (win.handle >= 0 && win.handle < g_win_count) {
        g_windows[win.handle].width = (float)w;
        g_windows[win.handle].height = (float)h;
        printf("[Window] Size: %ldx%ld\n", w, h);
    }
}

void window_set_resizable(Window win, int64_t resizable) {
    (void)win;
    printf("[Window] Resizable: %s\n", resizable ? "true" : "false");
}

void window_set_fullscreen(Window win, int64_t fullscreen) {
    (void)win;
    printf("[Window] Fullscreen: %s\n", fullscreen ? "true" : "false");
}

void window_close(Window win) {
    printf("[Window] Closed: %ld\n", win.handle);
}

void app_quit(App a) {
    (void)a;
    printf("[App] Quit\n");
}

/* Platform Functions */
const char* platform_name(void) { return "NeolyxOS"; }
const char* platform_version(void) { return "0.1.0"; }
int64_t screen_width(void) { return 1920; }
int64_t screen_height(void) { return 1080; }
double screen_scale(void) { return 1.0; }

/* Timer Stubs */
int64_t set_timeout(int64_t callback_id, int64_t delay_ms) {
    printf("[Timer] Timeout: callback=%ld, delay=%ldms\n", callback_id, delay_ms);
    return 0;
}

int64_t set_interval(int64_t callback_id, int64_t interval_ms) {
    printf("[Timer] Interval: callback=%ld, interval=%ldms\n", callback_id, interval_ms);
    return 0;
}

void clear_timer(int64_t timer_id) {
    printf("[Timer] Cleared: %ld\n", timer_id);
}


/* ============================================================================
 * Extended Colors
 * ============================================================================ */

Color color_rgb(int64_t r, int64_t g, int64_t b) {
    return (Color){r, g, b, 255};
}

Color color_hexa(int64_t hex, int64_t alpha) {
    return (Color){(hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, alpha};
}

Color color_alpha(Color c, int64_t a) {
    return (Color){c.r, c.g, c.b, a};
}

Color color_invert(Color c) {
    return (Color){255 - c.r, 255 - c.g, 255 - c.b, c.a};
}

Color color_grayscale(Color c) {
    int64_t gray = (c.r + c.g + c.b) / 3;
    return (Color){gray, gray, gray, c.a};
}

Color color_info(void) { return (Color){90, 200, 250, 255}; }
Color color_surface_alt(void) { return (Color){58, 58, 60, 255}; }
Color color_text_secondary(void) { return (Color){174, 174, 178, 255}; }
Color color_text_disabled(void) { return (Color){99, 99, 102, 255}; }
Color color_border_fn(void) { return (Color){72, 72, 74, 255}; }
Color color_divider_fn(void) { return (Color){56, 56, 58, 255}; }
Color color_overlay(void) { return (Color){0, 0, 0, 128}; }
Color color_clear(void) { return (Color){0, 0, 0, 0}; }
Color color_white(void) { return (Color){255, 255, 255, 255}; }
Color color_black(void) { return (Color){0, 0, 0, 255}; }

/* ============================================================================
 * Button Variants
 * ============================================================================ */

View button_primary(const char* label) {
    SimpleView* v = create_view(2, label);
    printf("[UI]   Button.primary: '%s'\n", label);
    return wrap_view(v);
}

View button_secondary(const char* label) {
    SimpleView* v = create_view(2, label);
    printf("[UI]   Button.secondary: '%s'\n", label);
    return wrap_view(v);
}

View button_tertiary(const char* label) {
    SimpleView* v = create_view(2, label);
    printf("[UI]   Button.tertiary: '%s'\n", label);
    return wrap_view(v);
}

View button_destructive(const char* label) {
    SimpleView* v = create_view(2, label);
    printf("[UI]   Button.destructive: '%s'\n", label);
    return wrap_view(v);
}

View button_icon(const char* icon) {
    SimpleView* v = create_view(2, icon);
    printf("[UI]   Button.icon: '%s'\n", icon);
    return wrap_view(v);
}

View button_icon_label(const char* icon, const char* label) {
    SimpleView* v = create_view(2, label);
    printf("[UI]   Button.icon_label: '%s' '%s'\n", icon, label);
    return wrap_view(v);
}

void button_set_loading(View v, int64_t loading) {
    (void)v;
    printf("[UI]     loading: %s\n", loading ? "true" : "false");
}

/* ============================================================================
 * Form Components
 * ============================================================================ */

View password_field(const char* placeholder) {
    SimpleView* v = create_view(3, placeholder);
    printf("[UI]   PasswordField: '%s'\n", placeholder);
    return wrap_view(v);
}

View search_field(const char* placeholder) {
    SimpleView* v = create_view(3, placeholder);
    printf("[UI]   SearchField: '%s'\n", placeholder);
    return wrap_view(v);
}

View email_field(const char* placeholder) {
    SimpleView* v = create_view(3, placeholder);
    printf("[UI]   EmailField: '%s'\n", placeholder);
    return wrap_view(v);
}

View number_field(const char* placeholder) {
    SimpleView* v = create_view(3, placeholder);
    printf("[UI]   NumberField: '%s'\n", placeholder);
    return wrap_view(v);
}

View radio_view(const char* label, int64_t selected) {
    SimpleView* v = create_view(5, label);
    printf("[UI]   Radio: '%s' (%s)\n", label, selected ? "selected" : "unselected");
    return wrap_view(v);
}

View switch_view(int64_t on) {
    SimpleView* v = create_view(6, "Switch");
    printf("[UI]   Switch: %s\n", on ? "on" : "off");
    return wrap_view(v);
}

View time_picker(void) {
    SimpleView* v = create_view(9, "TimePicker");
    printf("[UI] TimePicker()\n");
    return wrap_view(v);
}

View date_time_picker(void) {
    SimpleView* v = create_view(9, "DateTimePicker");
    printf("[UI] DateTimePicker()\n");
    return wrap_view(v);
}

View file_picker(void) {
    SimpleView* v = create_view(10, "FilePicker");
    printf("[UI] FilePicker()\n");
    return wrap_view(v);
}

View range_slider(double min_val, double max_val, double low, double high) {
    SimpleView* v = create_view(7, "RangeSlider");
    printf("[UI]   RangeSlider: %.0f-%.0f (low: %.0f, high: %.0f)\n", min_val, max_val, low, high);
    return wrap_view(v);
}

View stepper_view(int64_t min_val, int64_t max_val, int64_t value, int64_t step) {
    SimpleView* v = create_view(7, "Stepper");
    printf("[UI]   Stepper: %ld-%ld (value: %ld, step: %ld)\n", min_val, max_val, value, step);
    return wrap_view(v);
}

View form_view(void) {
    SimpleView* v = create_view(0, "Form");
    printf("[UI] Form()\n");
    return wrap_view(v);
}

View form_field(const char* label, View input) {
    (void)input;
    SimpleView* v = create_view(0, label);
    printf("[UI]   FormField: '%s'\n", label);
    return wrap_view(v);
}

View form_section(const char* title) {
    SimpleView* v = create_view(0, title);
    printf("[UI] FormSection: '%s'\n", title);
    return wrap_view(v);
}

View avatar_view(const char* source) {
    SimpleView* v = create_view(11, source);
    printf("[UI]   Avatar: '%s'\n", source);
    return wrap_view(v);
}

View badge_view(const char* text) {
    SimpleView* v = create_view(1, text);
    printf("[UI]   Badge: '%s'\n", text);
    return wrap_view(v);
}

View progress_view(double value) {
    SimpleView* v = create_view(7, "Progress");
    printf("[UI]   Progress: %.0f%%\n", value * 100);
    return wrap_view(v);
}

View activity_indicator(void) {
    SimpleView* v = create_view(7, "ActivityIndicator");
    printf("[UI] ActivityIndicator()\n");
    return wrap_view(v);
}

View flex_view(void) {
    SimpleView* v = create_view(0, "Flex");
    printf("[UI] Flex()\n");
    return wrap_view(v);
}

View center_view(void) {
    SimpleView* v = create_view(0, "Center");
    printf("[UI] Center()\n");
    return wrap_view(v);
}

/* ============================================================================
 * Debug Utilities
 * ============================================================================ */

void skip_view(View v) {
    (void)v;
    printf("[DEBUG] View skipped\n");
}

void ignore_val(int64_t value) {
    (void)value;
    /* Intentionally unused */
}

void debug_msg(const char* message) {
    printf("[DEBUG] %s\n", message);
}

void assert_fn(int64_t condition, const char* message) {
    if (!condition) {
        printf("[ASSERT FAILED] %s\n", message);
    }
}

void log_fn(int64_t level, const char* message) {
    const char* levels[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    printf("[%s] %s\n", level < 4 ? levels[level] : "LOG", message);
}

