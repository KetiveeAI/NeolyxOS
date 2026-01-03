/*
 * NeolyxOS Settings - Keyboard Panel
 * 
 * Keyboard layout settings with visual layouts for different keyboard sizes.
 * Supports Full (104 keys), TKL (87 keys), and Compact (60-65 keys).
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <nxkeyboard_layout.h>
#include <string.h>

/* ============================================================================
 * Keyboard Visual Representations for Different Sizes
 * ============================================================================ */

/* FULL SIZE KEYBOARD (104 keys with numpad and nav cluster) */
static const char* keyboard_visual_full[] = {
    "┌─────┐   ┌───┬───┬───┬───┐ ┌───┬───┬───┬───┐ ┌───┬───┬───┬───┐ ┌───┬───┬───┐",
    "│ ESC │   │F1 │F2 │F3 │F4 │ │F5 │F6 │F7 │F8 │ │F9 │F10│F11│F12│ │PRT│SCR│PAU│",
    "└─────┘   └───┴───┴───┴───┘ └───┴───┴───┴───┘ └───┴───┴───┴───┘ └───┴───┴───┘",
    "",
    "┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───────┐ ┌───┬───┬───┐ ┌───┬───┬───┬───┐",
    "│ ` │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 0 │ - │ = │ BACKS │ │INS│HOM│PUP│ │NUM│ / │ * │ - │",
    "├───┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─────┤ ├───┼───┼───┤ ├───┼───┼───┼───┤",
    "│ TAB │ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │ [ │ ] │  \\  │ │DEL│END│PDN│ │ 7 │ 8 │ 9 │   │",
    "├─────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴─────┤ └───┴───┴───┘ ├───┼───┼───┤ + │",
    "│ MODE │ A │ S │ D │ F │ G │ H │ J │ K │ L │ ; │ ' │ ALLOW  │               │ 4 │ 5 │ 6 │   │",
    "├──────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴────────┤     ┌───┐     ├───┼───┼───┼───┤",
    "│ SHIFT  │ Z │ X │ C │ V │ B │ N │ M │ , │ . │ / │  SHIFT   │     │ ↑ │     │ 1 │ 2 │ 3 │   │",
    "├────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴───┼───┴┬────┬────┤ ┌───┼───┼───┐ ├───┴───┼───┤ENT│",
    "│CTRL│ FN │ NX │         SPACE          │ NX │ALT │CTRL│ MN │ │ ← │ ↓ │ → │ │   0   │ . │   │",
    "└────┴────┴────┴────────────────────────┴────┴────┴────┴────┘ └───┴───┴───┘ └───────┴───┴───┘",
    NULL
};

/* TKL KEYBOARD (87 keys - no numpad) */
static const char* keyboard_visual_tkl[] = {
    "┌─────┐   ┌───┬───┬───┬───┐ ┌───┬───┬───┬───┐ ┌───┬───┬───┬───┐ ┌───┬───┬───┐",
    "│ ESC │   │F1 │F2 │F3 │F4 │ │F5 │F6 │F7 │F8 │ │F9 │F10│F11│F12│ │PRT│SCR│PAU│",
    "└─────┘   └───┴───┴───┴───┘ └───┴───┴───┴───┘ └───┴───┴───┴───┘ └───┴───┴───┘",
    "",
    "┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───────┐ ┌───┬───┬───┐",
    "│ ` │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 0 │ - │ = │ BACKS │ │INS│HOM│PUP│",
    "├───┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─────┤ ├───┼───┼───┤",
    "│ TAB │ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │ [ │ ] │  \\  │ │DEL│END│PDN│",
    "├─────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴─────┤ └───┴───┴───┘",
    "│ MODE │ A │ S │ D │ F │ G │ H │ J │ K │ L │ ; │ ' │ ALLOW  │",
    "├──────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴────────┤     ┌───┐",
    "│ SHIFT  │ Z │ X │ C │ V │ B │ N │ M │ , │ . │ / │  SHIFT   │     │ ↑ │",
    "├────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴───┼───┴┬────┬────┤ ┌───┼───┼───┐",
    "│CTRL│ FN │ NX │         SPACE          │ NX │ALT │CTRL│ MN │ │ ← │ ↓ │ → │",
    "└────┴────┴────┴────────────────────────┴────┴────┴────┴────┘ └───┴───┴───┘",
    NULL
};

/* COMPACT KEYBOARD (60-65 keys - laptop style) */
static const char* keyboard_visual_compact[] = {
    "┌─────┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬─────────┐",
    "│ ESC │S1 │S2 │S3 │S4 │S5 │S6 │S7 │S8 │S9 │S10│S11│S12│  DEL    │",
    "├─────┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼─────────┤",
    "│  `  │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 0 │ - │ = │ BACKSP  │",
    "├─────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬───────┤",
    "│ TAB   │ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │ [ │ ] │   \\   │",
    "├───────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴───────┤",
    "│ MODE   │ A │ S │ D │ F │ G │ H │ J │ K │ L │ ; │ ' │  ALLOW   │",
    "├────────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴──────────┤",
    "│  SHIFT   │ Z │ X │ C │ V │ B │ N │ M │ , │ . │ / │   SHIFT    │",
    "├──────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴───┼───┴┬─────┬─────┤",
    "│ CTRL │ FN │ NX │         SPACE          │ NX │ALT │CTRL │ARRW │",
    "└──────┴────┴────┴────────────────────────┴────┴────┴─────┴─────┘",
    NULL
};

/* Current keyboard size (default: compact for most laptops) */
static keyboard_size_t current_size = KEYBOARD_SIZE_COMPACT;

/* Keyboard layout options */
static const char* layout_names[] = {
    "US QWERTY (Default)",
    "US Dvorak",
    "UK English",
    "German QWERTZ",
    "French AZERTY",
    "Spanish",
    "Italian",
    "Portuguese",
    "Russian",
    "Japanese",
    "Korean",
    "Chinese Pinyin",
    "Custom"
};

/* Keyboard size names */
static const char* size_names[] = {
    "Full (104 keys)",
    "TKL (87 keys)",
    "75%",
    "65%",
    "60%",
    "Compact/Laptop"
};

/* Get visual for current size */
static const char** get_keyboard_visual(keyboard_size_t size) {
    switch (size) {
        case KEYBOARD_SIZE_FULL:
            return keyboard_visual_full;
        case KEYBOARD_SIZE_TKL:
            return keyboard_visual_tkl;
        case KEYBOARD_SIZE_75:
        case KEYBOARD_SIZE_65:
        case KEYBOARD_SIZE_60:
        case KEYBOARD_SIZE_COMPACT:
        default:
            return keyboard_visual_compact;
    }
}

/* Create keyboard visual widget */
static rx_view* create_keyboard_visual(keyboard_size_t size) {
    rx_view* container = vstack_new(2.0f);
    if (!container) return NULL;
    
    container->box.background = (rx_color){ 28, 28, 30, 255 };
    container->box.padding = insets_all(16.0f);
    container->box.corner_radius = corners_all(8.0f);
    
    const char** visual = get_keyboard_visual(size);
    
    /* Add each line of the keyboard visual */
    for (int i = 0; visual[i] != NULL; i++) {
        if (visual[i][0] == '\0') {
            /* Empty line - add spacing */
            rx_view* spacer = vstack_new(0);
            if (spacer) {
                spacer->box.min_height = 4.0f;
                view_add_child(container, spacer);
            }
            continue;
        }
        
        rx_text_view* line = text_view_new(visual[i]);
        if (line) {
            /* Use monospace font for proper alignment */
            text_view_set_font_size(line, 10.0f);
            
            /* Highlight special keys with colors */
            if (strstr(visual[i], "NX")) {
                line->color = (rx_color){ 0, 122, 255, 255 };      /* Blue for NX keys */
            } else if (strstr(visual[i], "MODE") || strstr(visual[i], "ALLOW")) {
                line->color = (rx_color){ 52, 199, 89, 255 };      /* Green for NeolyxOS keys */
            } else if (strstr(visual[i], "PRT") || strstr(visual[i], "SCR") || 
                       strstr(visual[i], "PAU") || strstr(visual[i], "INS") ||
                       strstr(visual[i], "HOM") || strstr(visual[i], "PUP") ||
                       strstr(visual[i], "DEL") || strstr(visual[i], "END") ||
                       strstr(visual[i], "PDN") || strstr(visual[i], "NUM")) {
                line->color = (rx_color){ 255, 149, 0, 255 };      /* Orange for nav/numpad */
            } else {
                line->color = (rx_color){ 174, 174, 178, 255 };
            }
            
            view_add_child(container, (rx_view*)line);
        }
    }
    
    return container;
}

/* Create keyboard size selector */
static rx_view* create_size_selector(void) {
    rx_view* card = settings_card("Keyboard Size");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new(
        "Select your keyboard type. Full keyboards have numpad and navigation keys. "
        "On compact keyboards, use Fn+Arrow or NX+Arrow for Page Up/Down, Home/End."
    );
    if (desc) {
        desc->color = NX_COLOR_TEXT_DIM;
        text_view_set_font_size(desc, 12.0f);
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Size buttons */
    rx_view* size_row = hstack_new(8.0f);
    if (size_row) {
        for (int i = 0; i < 3; i++) {  /* Show only Full, TKL, Compact */
            int idx = (i == 2) ? KEYBOARD_SIZE_COMPACT : i;
            rx_button_view* btn = button_view_new(size_names[idx]);
            if (btn) {
                if (idx == (int)current_size) {
                    btn->normal_color = NX_COLOR_PRIMARY;
                } else {
                    btn->normal_color = (rx_color){ 60, 60, 62, 255 };
                }
                btn->hover_color = (rx_color){ 80, 80, 82, 255 };
                view_add_child(size_row, (rx_view*)btn);
            }
        }
        view_add_child(card, size_row);
    }
    
    return card;
}

/* Create NX key info card */
static rx_view* create_nx_key_info(void) {
    rx_view* card = settings_card("NX Key (NeolyxOS Key)");
    if (!card) return NULL;
    
    /* Description */
    rx_text_view* desc = text_view_new(
        "The NX key replaces the Windows/Super key and opens the App Drawer. "
        "Combined with other keys, it provides system shortcuts."
    );
    if (desc) {
        desc->color = NX_COLOR_TEXT_DIM;
        text_view_set_font_size(desc, 13.0f);
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Shortcuts list */
    rx_view* shortcuts = settings_section("Common Shortcuts");
    if (shortcuts) view_add_child(card, shortcuts);
    
    const char* shortcut_list[] = {
        "NX (alone)     →  App Drawer",
        "NX + Space     →  Spotlight Search",
        "NX + Tab       →  Window Switcher",
        "NX + Q         →  Quit Application",
        "NX + W         →  Close Window",
        "NX + L         →  Lock Screen",
        "NX + E         →  File Manager",
        "NX + D         →  Show Desktop",
        "NX + ,         →  Settings"
    };
    
    for (int i = 0; i < 9; i++) {
        rx_text_view* item = text_view_new(shortcut_list[i]);
        if (item) {
            item->color = NX_COLOR_TEXT;
            text_view_set_font_size(item, 12.0f);
            view_add_child(card, (rx_view*)item);
        }
    }
    
    return card;
}

/* Create navigation keys info (for full/TKL keyboards) */
static rx_view* create_nav_keys_info(void) {
    rx_view* card = settings_card("Navigation Keys");
    if (!card) return NULL;
    
    /* Full keyboard navigation */
    rx_view* full_section = vstack_new(4.0f);
    if (full_section) {
        rx_text_view* title = text_view_new("Full/TKL Keyboard Navigation:");
        if (title) {
            title->font_weight = 600;
            title->color = (rx_color){ 255, 149, 0, 255 };
            view_add_child(full_section, (rx_view*)title);
        }
        
        const char* nav_keys[] = {
            "Print Screen  →  Screenshot (or NX + Shift + 3)",
            "Scroll Lock   →  Toggle scroll mode",
            "Pause/Break   →  Pause system processes",
            "Insert        →  Toggle insert mode",
            "Home          →  Go to beginning",
            "End           →  Go to end",
            "Page Up       →  Scroll up one page",
            "Page Down     →  Scroll down one page"
        };
        
        for (int i = 0; i < 8; i++) {
            rx_text_view* item = text_view_new(nav_keys[i]);
            if (item) {
                item->color = NX_COLOR_TEXT_DIM;
                text_view_set_font_size(item, 12.0f);
                view_add_child(full_section, (rx_view*)item);
            }
        }
        view_add_child(card, full_section);
    }
    
    /* Compact keyboard alternatives */
    rx_view* compact_section = vstack_new(4.0f);
    if (compact_section) {
        rx_text_view* title2 = text_view_new("Compact Keyboard Alternatives:");
        if (title2) {
            title2->font_weight = 600;
            title2->color = NX_COLOR_TEXT;
            view_add_child(compact_section, (rx_view*)title2);
        }
        
        const char* compact_nav[] = {
            "Fn + ↑  =  Page Up",
            "Fn + ↓  =  Page Down",
            "Fn + ←  =  Home",
            "Fn + →  =  End",
            "NX + Shift + 3  =  Print Screen"
        };
        
        for (int i = 0; i < 5; i++) {
            rx_text_view* item = text_view_new(compact_nav[i]);
            if (item) {
                item->color = NX_COLOR_TEXT_DIM;
                text_view_set_font_size(item, 12.0f);
                view_add_child(compact_section, (rx_view*)item);
            }
        }
        view_add_child(card, compact_section);
    }
    
    return card;
}

/* Create numpad info (for full keyboards) */
static rx_view* create_numpad_info(void) {
    rx_view* card = settings_card("Numpad (Full Keyboard Only)");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new(
        "The numeric keypad provides quick number entry and calculation keys. "
        "Num Lock toggles between numbers and navigation functions."
    );
    if (desc) {
        desc->color = NX_COLOR_TEXT_DIM;
        text_view_set_font_size(desc, 12.0f);
        view_add_child(card, (rx_view*)desc);
    }
    
    rx_text_view* numlock = text_view_new("Num Lock ON: 0-9 number keys");
    if (numlock) {
        numlock->color = NX_COLOR_TEXT;
        view_add_child(card, (rx_view*)numlock);
    }
    
    rx_text_view* numoff = text_view_new("Num Lock OFF: Navigation keys (Home, End, PgUp, PgDn, Arrows)");
    if (numoff) {
        numoff->color = NX_COLOR_TEXT;
        view_add_child(card, (rx_view*)numoff);
    }
    
    return card;
}

/* Create special keys info card */
static rx_view* create_special_keys_info(void) {
    rx_view* card = settings_card("Special NeolyxOS Keys");
    if (!card) return NULL;
    
    /* MODE key */
    rx_view* mode_section = hstack_new(8.0f);
    if (mode_section) {
        rx_text_view* mode_label = text_view_new("MODE Key:");
        if (mode_label) {
            mode_label->font_weight = 600;
            mode_label->color = (rx_color){ 52, 199, 89, 255 };
            view_add_child(mode_section, (rx_view*)mode_label);
        }
        rx_text_view* mode_desc = text_view_new("System modifier, replaces Caps Lock");
        if (mode_desc) {
            mode_desc->color = NX_COLOR_TEXT_DIM;
            view_add_child(mode_section, (rx_view*)mode_desc);
        }
        view_add_child(card, mode_section);
    }
    
    /* ALLOW key */
    rx_view* allow_section = hstack_new(8.0f);
    if (allow_section) {
        rx_text_view* allow_label = text_view_new("ALLOW Key:");
        if (allow_label) {
            allow_label->font_weight = 600;
            allow_label->color = (rx_color){ 52, 199, 89, 255 };
            view_add_child(allow_section, (rx_view*)allow_label);
        }
        rx_text_view* allow_desc = text_view_new("Semantic Enter, confirms actions");
        if (allow_desc) {
            allow_desc->color = NX_COLOR_TEXT_DIM;
            view_add_child(allow_section, (rx_view*)allow_desc);
        }
        view_add_child(card, allow_section);
    }
    
    /* Function row */
    rx_view* fn_section = vstack_new(4.0f);
    if (fn_section) {
        rx_text_view* fn_label = text_view_new("Function Row (S1-S12 / F1-F12):");
        if (fn_label) {
            fn_label->font_weight = 600;
            fn_label->color = NX_COLOR_TEXT;
            view_add_child(fn_section, (rx_view*)fn_label);
        }
        rx_text_view* fn_desc = text_view_new(
            "Shift + Function keys trigger media controls:\n"
            "F1=Play, F2=Stop, F3=Prev, F4=Next, F5=Mute,\n"
            "F6=Vol-, F7=Vol+, F8=Bright-, F9=Bright+,\n"
            "F10=Display, F11=WiFi, F12=Airplane"
        );
        if (fn_desc) {
            fn_desc->color = NX_COLOR_TEXT_DIM;
            text_view_set_font_size(fn_desc, 12.0f);
            view_add_child(fn_section, (rx_view*)fn_desc);
        }
        view_add_child(card, fn_section);
    }
    
    return card;
}

/* Create keyboard layout selector */
static rx_view* create_layout_selector(settings_ctx_t* ctx) {
    rx_view* card = settings_card("Keyboard Layout");
    if (!card) return NULL;
    
    /* Current layout display */
    keyboard_layout_t current = nxkeyboard_get_layout();
    const char* current_name = layout_names[current < LAYOUT_CUSTOM + 1 ? current : 0];
    
    rx_view* current_row = settings_row("Current Layout", current_name);
    if (current_row) view_add_child(card, current_row);
    
    /* Layout buttons */
    rx_view* layout_grid = vstack_new(4.0f);
    if (layout_grid) {
        for (int i = 0; i <= LAYOUT_CUSTOM; i++) {
            rx_button_view* btn = button_view_new(layout_names[i]);
            if (btn) {
                if (i == (int)current) {
                    btn->normal_color = NX_COLOR_PRIMARY;
                } else {
                    btn->normal_color = (rx_color){ 60, 60, 62, 255 };
                }
                btn->hover_color = (rx_color){ 80, 80, 82, 255 };
                view_add_child(layout_grid, (rx_view*)btn);
            }
        }
        view_add_child(card, layout_grid);
    }
    
    return card;
}

/* ============================================================================
 * Main Panel Creation
 * ============================================================================ */

rx_view* keyboard_panel_create(settings_ctx_t* ctx) {
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Panel header */
    rx_text_view* header = text_view_new("Keyboard");
    if (header) {
        text_view_set_font_size(header, 28.0f);
        header->font_weight = 700;
        header->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)header);
    }
    
    /* Subtitle */
    rx_text_view* subtitle = text_view_new("NeolyxOS keyboard layout and input settings");
    if (subtitle) {
        subtitle->color = NX_COLOR_TEXT_DIM;
        view_add_child(panel, (rx_view*)subtitle);
    }
    
    /* Keyboard size selector */
    rx_view* size_select = create_size_selector();
    if (size_select) view_add_child(panel, size_select);
    
    /* Keyboard visual */
    rx_view* visual_section = settings_section("Keyboard Layout");
    if (visual_section) view_add_child(panel, visual_section);
    
    rx_view* keyboard_visual_view = create_keyboard_visual(current_size);
    if (keyboard_visual_view) view_add_child(panel, keyboard_visual_view);
    
    /* Navigation keys info */
    rx_view* nav_info = create_nav_keys_info();
    if (nav_info) view_add_child(panel, nav_info);
    
    /* Numpad info (only show for full keyboard) */
    if (current_size == KEYBOARD_SIZE_FULL) {
        rx_view* numpad_info = create_numpad_info();
        if (numpad_info) view_add_child(panel, numpad_info);
    }
    
    /* NX key information */
    rx_view* nx_info = create_nx_key_info();
    if (nx_info) view_add_child(panel, nx_info);
    
    /* Special keys */
    rx_view* special_keys = create_special_keys_info();
    if (special_keys) view_add_child(panel, special_keys);
    
    /* Layout selector */
    rx_view* layout_select = create_layout_selector(ctx);
    if (layout_select) view_add_child(panel, layout_select);
    
    return panel;
}
