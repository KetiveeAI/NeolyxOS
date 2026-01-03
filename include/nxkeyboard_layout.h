/*
 * NeolyxOS Keyboard Layout
 * 
 * Supports multiple keyboard sizes:
 * - Full (104/105 keys with numpad and nav cluster)
 * - TKL (87 keys, no numpad)
 * - Compact (60-65 keys, laptop style)
 * 
 * Standard US QWERTY with NeolyxOS-specific keys.
 * The "NX" key replaces the Windows/Super key.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXKEYBOARD_LAYOUT_H
#define NXKEYBOARD_LAYOUT_H

#include <stdint.h>

/* ============ Keyboard Size Types ============ */

typedef enum {
    KEYBOARD_SIZE_FULL,         /* 104/105 keys: numpad + nav cluster */
    KEYBOARD_SIZE_TKL,          /* 87/88 keys: no numpad */
    KEYBOARD_SIZE_75,           /* 75%: compact with F-row */
    KEYBOARD_SIZE_65,           /* 65%: no F-row, has arrows */
    KEYBOARD_SIZE_60,           /* 60%: no F-row, no arrows, no nav */
    KEYBOARD_SIZE_COMPACT,      /* Laptop-style compact */
} keyboard_size_t;

/* ============ NeolyxOS Special Keys ============ */

/*
 * KEY_NX (NeolyxOS Key)
 * - Same physical position as Windows/Super key
 * - Opens App Drawer / Launchpad
 * - Combined with other keys for shortcuts:
 *   - NX + Space: Spotlight search
 *   - NX + Tab: Window switcher
 *   - NX + Q: Quit application
 *   - NX + W: Close window
 *   - NX + E: Open file manager (Path)
 *   - NX + A: Select all
 *   - NX + S: Save
 *   - NX + D: Desktop
 *   - NX + L: Lock screen
 *   - NX + ,: Settings
 */
#define KEY_NX              0x5B  /* Left NX key (same scancode as Windows key) */
#define KEY_NX_RIGHT        0x5C  /* Right NX key */

/* ============ Navigation Cluster Keys (Full/TKL) ============ */

#define KEY_PRINT_SCREEN    0x37  /* E0 37: Print Screen / SysRq */
#define KEY_SCROLL_LOCK     0x46  /* Scroll Lock */
#define KEY_PAUSE           0x45  /* E1 1D 45: Pause / Break */

#define KEY_INSERT          0x52  /* E0 52: Insert */
#define KEY_HOME            0x47  /* E0 47: Home */
#define KEY_PAGE_UP         0x49  /* E0 49: Page Up */
#define KEY_DELETE          0x53  /* E0 53: Delete */
#define KEY_END             0x4F  /* E0 4F: End */
#define KEY_PAGE_DOWN       0x51  /* E0 51: Page Down */

/* Arrow keys */
#define KEY_UP              0x48  /* E0 48: Up Arrow */
#define KEY_DOWN            0x50  /* E0 50: Down Arrow */
#define KEY_LEFT            0x4B  /* E0 4B: Left Arrow */
#define KEY_RIGHT           0x4D  /* E0 4D: Right Arrow */

/* ============ Numpad Keys (Full only) ============ */

#define KEY_NUM_LOCK        0x45  /* Num Lock */
#define KEY_KP_SLASH        0x35  /* E0 35: Numpad / */
#define KEY_KP_ASTERISK     0x37  /* Numpad * */
#define KEY_KP_MINUS        0x4A  /* Numpad - */
#define KEY_KP_PLUS         0x4E  /* Numpad + */
#define KEY_KP_ENTER        0x1C  /* E0 1C: Numpad Enter */
#define KEY_KP_DECIMAL      0x53  /* Numpad . */

#define KEY_KP_0            0x52  /* Numpad 0 */
#define KEY_KP_1            0x4F  /* Numpad 1 */
#define KEY_KP_2            0x50  /* Numpad 2 */
#define KEY_KP_3            0x51  /* Numpad 3 */
#define KEY_KP_4            0x4B  /* Numpad 4 */
#define KEY_KP_5            0x4C  /* Numpad 5 */
#define KEY_KP_6            0x4D  /* Numpad 6 */
#define KEY_KP_7            0x47  /* Numpad 7 */
#define KEY_KP_8            0x48  /* Numpad 8 */
#define KEY_KP_9            0x49  /* Numpad 9 */

/* ============ Modifier Flags ============ */

#define MOD_NX              0x10  /* NeolyxOS key modifier */
#define MOD_FN              0x20  /* Function key modifier */
#define MOD_SHIFT           0x01  /* Shift modifier */
#define MOD_CTRL            0x02  /* Control modifier */
#define MOD_ALT             0x04  /* Alt modifier */

/* ============ Special NeolyxOS Shortcuts ============ */

typedef enum {
    /* Window management */
    NX_SHORTCUT_QUIT,           /* NX + Q */
    NX_SHORTCUT_CLOSE_WINDOW,   /* NX + W */
    NX_SHORTCUT_MINIMIZE,       /* NX + M */
    NX_SHORTCUT_HIDE,           /* NX + H */
    NX_SHORTCUT_FULLSCREEN,     /* NX + F */
    
    /* Navigation */
    NX_SHORTCUT_APP_DRAWER,     /* NX (alone) */
    NX_SHORTCUT_SPOTLIGHT,      /* NX + Space */
    NX_SHORTCUT_WINDOW_SWITCH,  /* NX + Tab */
    NX_SHORTCUT_DESKTOP,        /* NX + D */
    NX_SHORTCUT_MISSION_CTRL,   /* NX + Up */
    
    /* System */
    NX_SHORTCUT_LOCK,           /* NX + L */
    NX_SHORTCUT_SETTINGS,       /* NX + , */
    NX_SHORTCUT_SCREENSHOT,     /* NX + Shift + 3 or Print Screen */
    NX_SHORTCUT_SCREEN_REGION,  /* NX + Shift + 4 */
    NX_SHORTCUT_SCREEN_RECORD,  /* NX + Shift + 5 */
    
    /* File operations */
    NX_SHORTCUT_SAVE,           /* NX + S */
    NX_SHORTCUT_OPEN,           /* NX + O */
    NX_SHORTCUT_NEW,            /* NX + N */
    NX_SHORTCUT_PRINT,          /* NX + P */
    
    /* Edit operations */
    NX_SHORTCUT_COPY,           /* NX + C */
    NX_SHORTCUT_PASTE,          /* NX + V */
    NX_SHORTCUT_CUT,            /* NX + X */
    NX_SHORTCUT_UNDO,           /* NX + Z */
    NX_SHORTCUT_REDO,           /* NX + Shift + Z */
    NX_SHORTCUT_SELECT_ALL,     /* NX + A */
    NX_SHORTCUT_FIND,           /* NX + F */
    
    /* Applications */
    NX_SHORTCUT_FILE_MANAGER,   /* NX + E */
    NX_SHORTCUT_TERMINAL,       /* NX + T */
    NX_SHORTCUT_BROWSER,        /* NX + B */
    
    /* Page navigation */
    NX_SHORTCUT_PAGE_UP,        /* Page Up or NX + Up (on compact) */
    NX_SHORTCUT_PAGE_DOWN,      /* Page Down or NX + Down (on compact) */
    NX_SHORTCUT_HOME,           /* Home or NX + Left (on compact) */
    NX_SHORTCUT_END,            /* End or NX + Right (on compact) */
    
} nx_shortcut_t;

/* ============ Keyboard Layouts ============ */

typedef enum {
    LAYOUT_US_QWERTY,       /* Default */
    LAYOUT_US_DVORAK,
    LAYOUT_UK,
    LAYOUT_DE_QWERTZ,
    LAYOUT_FR_AZERTY,
    LAYOUT_ES,
    LAYOUT_IT,
    LAYOUT_PT,
    LAYOUT_RU,
    LAYOUT_JP,
    LAYOUT_KR,
    LAYOUT_CN_PINYIN,
    LAYOUT_CUSTOM
} keyboard_layout_t;

/* ============ Key Labels for Visual Representation ============ */

/*
 * FULL SIZE KEYBOARD (104/105 keys)
 * 
 * ┌─────┐   ┌───┬───┬───┬───┐ ┌───┬───┬───┬───┐ ┌───┬───┬───┬───┐ ┌───┬───┬───┐
 * │ ESC │   │F1 │F2 │F3 │F4 │ │F5 │F6 │F7 │F8 │ │F9 │F10│F11│F12│ │PRT│SCR│PAU│
 * └─────┘   └───┴───┴───┴───┘ └───┴───┴───┴───┘ └───┴───┴───┴───┘ └───┴───┴───┘
 * 
 * ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───────┐ ┌───┬───┬───┐ ┌───┬───┬───┬───┐
 * │ ` │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 0 │ - │ = │ BACKS │ │INS│HOM│PUP│ │NUM│ / │ * │ - │
 * ├───┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─────┤ ├───┼───┼───┤ ├───┼───┼───┼───┤
 * │ TAB │ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │ [ │ ] │  \  │ │DEL│END│PDN│ │ 7 │ 8 │ 9 │   │
 * ├─────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴─────┤ └───┴───┴───┘ ├───┼───┼───┤ + │
 * │ MODE │ A │ S │ D │ F │ G │ H │ J │ K │ L │ ; │ ' │ ALLOW  │               │ 4 │ 5 │ 6 │   │
 * ├──────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴────────┤     ┌───┐     ├───┼───┼───┼───┤
 * │ SHIFT  │ Z │ X │ C │ V │ B │ N │ M │ , │ . │ / │  SHIFT   │     │ ↑ │     │ 1 │ 2 │ 3 │   │
 * ├────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴───┼───┴┬────┬────┤ ┌───┼───┼───┐ ├───┴───┼───┤ENT│
 * │CTRL│ FN │ NX │         SPACE          │ NX │ALT │CTRL│ MN │ │ ← │ ↓ │ → │ │   0   │ . │   │
 * └────┴────┴────┴────────────────────────┴────┴────┴────┴────┘ └───┴───┴───┘ └───────┴───┴───┘
 * 
 * NX = NeolyxOS Key (blue accent, opens App Drawer)
 * MODE = Mode key (Caps Lock position)
 * ALLOW = Allow/Enter key
 * MN = Menu key
 * PRT = Print Screen, SCR = Scroll Lock, PAU = Pause
 * INS = Insert, HOM = Home, PUP = Page Up
 * DEL = Delete, END = End, PDN = Page Down
 * NUM = Num Lock
 */

/*
 * TKL KEYBOARD (87/88 keys) - Same as full without numpad
 * 
 * ┌─────┐   ┌───┬───┬───┬───┐ ┌───┬───┬───┬───┐ ┌───┬───┬───┬───┐ ┌───┬───┬───┐
 * │ ESC │   │F1 │F2 │F3 │F4 │ │F5 │F6 │F7 │F8 │ │F9 │F10│F11│F12│ │PRT│SCR│PAU│
 * └─────┘   └───┴───┴───┴───┘ └───┴───┴───┴───┘ └───┴───┴───┴───┘ └───┴───┴───┘
 * 
 * ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───────┐ ┌───┬───┬───┐
 * │ ` │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 0 │ - │ = │ BACKS │ │INS│HOM│PUP│
 * ├───┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─────┤ ├───┼───┼───┤
 * │ TAB │ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │ [ │ ] │  \  │ │DEL│END│PDN│
 * ├─────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴─────┤ └───┴───┴───┘
 * │ MODE │ A │ S │ D │ F │ G │ H │ J │ K │ L │ ; │ ' │ ALLOW  │
 * ├──────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴────────┤     ┌───┐
 * │ SHIFT  │ Z │ X │ C │ V │ B │ N │ M │ , │ . │ / │  SHIFT   │     │ ↑ │
 * ├────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴───┼───┴┬────┬────┤ ┌───┼───┼───┐
 * │CTRL│ FN │ NX │         SPACE          │ NX │ALT │CTRL│ MN │ │ ← │ ↓ │ → │
 * └────┴────┴────┴────────────────────────┴────┴────┴────┴────┘ └───┴───┴───┘
 */

/*
 * COMPACT/LAPTOP KEYBOARD (60-65 keys)
 * Navigation via Fn+Arrow or NX+Arrow combinations
 * 
 * ┌─────┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬─────────┐
 * │ ESC │S1 │S2 │S3 │S4 │S5 │S6 │S7 │S8 │S9 │S10│S11│S12│  DEL    │
 * ├─────┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼─────────┤
 * │  `  │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 0 │ - │ = │ BACKSP  │
 * ├─────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬───────┤
 * │ TAB   │ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │ [ │ ] │   \   │
 * ├───────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴───────┤
 * │ MODE   │ A │ S │ D │ F │ G │ H │ J │ K │ L │ ; │ ' │  ALLOW   │
 * ├────────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴──────────┤
 * │  SHIFT   │ Z │ X │ C │ V │ B │ N │ M │ , │ . │ / │   SHIFT    │
 * ├──────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴───┼───┴┬─────┬─────┤
 * │ CTRL │ FN │ NX │         SPACE          │ NX │ALT │CTRL │ARRW │
 * └──────┴────┴────┴────────────────────────┴────┴────┴─────┴─────┘
 * 
 * Compact Navigation Shortcuts:
 * - Fn + ↑ = Page Up
 * - Fn + ↓ = Page Down  
 * - Fn + ← = Home
 * - Fn + → = End
 * - NX + ← = Home (alternate)
 * - NX + → = End (alternate)
 * - Fn + S1-S12 = F1-F12
 * - Shift + S1-S12 = Media keys
 */

/* ============ Keyboard State ============ */

typedef struct {
    keyboard_size_t size;           /* Physical keyboard size */
    keyboard_layout_t layout;       /* Key layout (QWERTY, etc.) */
    uint8_t modifiers;              /* Current modifier state */
    uint8_t num_lock;               /* Num Lock state */
    uint8_t scroll_lock;            /* Scroll Lock state */
    uint8_t mode_key;               /* MODE key state */
    uint8_t fn_key;                 /* FN key state */
    uint8_t nx_key;                 /* NX key state */
} keyboard_state_t;

/* ============ API ============ */

/* Set keyboard layout */
int nxkeyboard_set_layout(keyboard_layout_t layout);
keyboard_layout_t nxkeyboard_get_layout(void);

/* Set keyboard size */
int nxkeyboard_set_size(keyboard_size_t size);
keyboard_size_t nxkeyboard_get_size(void);

/* Check if NX key is pressed */
int nxkeyboard_is_nx_pressed(void);

/* Handle system shortcuts */
typedef void (*nx_shortcut_handler_t)(nx_shortcut_t shortcut, void *ctx);
void nxkeyboard_set_shortcut_handler(nx_shortcut_handler_t handler, void *ctx);

/* Get shortcut from key combo */
nx_shortcut_t nxkeyboard_get_shortcut(uint8_t key, uint8_t modifiers);

/* Navigation key helpers for compact keyboards */
uint8_t nxkeyboard_translate_nav_key(uint8_t key, uint8_t modifiers, keyboard_size_t size);

/* Check if key is a navigation key */
int nxkeyboard_is_nav_key(uint8_t key);

/* Get keyboard state */
const keyboard_state_t* nxkeyboard_get_state(void);

#endif /* NXKEYBOARD_LAYOUT_H */
