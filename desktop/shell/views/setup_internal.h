/*
 * NeolyxOS - Setup Assistant: private interfaces shared by setup_*.c
 *
 * Not a public API. setup_assistant.h is the only header other shell
 * components may include.
 *
 *   setup_assistant.c  flow state, transitions, frame chrome, input
 *   setup_pages.c      per-page layout, drawing and page logic
 *   setup_store.c      validation, data tables, persistence, the
 *                      accounts/network client seams, secure wipe
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef SETUP_INTERNAL_H
#define SETUP_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "nxcanvas.h"
#include "nxui.h"
#include "nxglass.h"
#include "setup_assistant.h"

/* ============ Result codes ============ */

#define SETUP_OK                  0
#define SETUP_ERR_INVALID        -1   /* bad argument */
#define SETUP_ERR_UNSUPPORTED    -2   /* no system service behind this call yet */
#define SETUP_ERR_IO             -3   /* config / file write failed */
#define SETUP_ERR_NOT_FOUND      -4

/* ============ Pages ============ */

typedef enum {
    SP_WELCOME = 0,
    SP_LANGUAGE,
    SP_REGION,
    SP_ACCESSIBILITY,
    SP_NETWORK,
    SP_ACCOUNT,
    SP_KETIVEE,
    SP_PRIVACY,
    SP_APPEARANCE,
    SP_SETTING_UP,
    SP_DONE,
    SP_COUNT
} setup_page_t;

/* Pages shown in the step indicator: SP_LANGUAGE .. SP_APPEARANCE */
#define SETUP_FIRST_STEP   SP_LANGUAGE
#define SETUP_LAST_STEP    SP_APPEARANCE
#define SETUP_STEP_COUNT   (SETUP_LAST_STEP - SETUP_FIRST_STEP + 1)

/* ============ Text fields ============ */

#define SETUP_FIELD_CAP 64            /* bytes including NUL */

typedef struct {
    char    buf[SETUP_FIELD_CAP];
    uint8_t len;
    uint8_t secret;                   /* 1 = masked, wiped with setup_secure_wipe */
    uint8_t touched;                  /* user typed in it at least once */
} setup_field_t;

/* ============ Tables ============ */

typedef struct { const char *code; const char *name; const char *detail; } setup_choice_t;

extern const setup_choice_t g_setup_languages[];
extern const int            g_setup_language_count;
extern const setup_choice_t g_setup_regions[];       /* detail = suggested keyboard code */
extern const int            g_setup_region_count;
extern const setup_choice_t g_setup_keyboards[];
extern const int            g_setup_keyboard_count;
extern const char *const    g_setup_hello[];
extern const int            g_setup_hello_count;

typedef struct { const char *name; nxc_color_t color; } setup_accent_t;
extern const setup_accent_t g_setup_accents[];
extern const int            g_setup_accent_count;

/* Accessibility and privacy switches (order = on-screen order) */
enum { SETUP_AX_LARGER_TEXT = 0, SETUP_AX_BOLD_TEXT, SETUP_AX_REDUCE_MOTION,
       SETUP_AX_INCREASE_CONTRAST, SETUP_AX_REDUCE_TRANSPARENCY, SETUP_AX_COUNT };
enum { SETUP_PRIV_ANALYTICS = 0, SETUP_PRIV_CRASH_REPORTS, SETUP_PRIV_SUGGESTIONS,
       SETUP_PRIV_LOCATION, SETUP_PRIV_COUNT };

/* Appearance choice */
enum { SETUP_APPEAR_LIGHT = 0, SETUP_APPEAR_DARK, SETUP_APPEAR_AUTO, SETUP_APPEAR_COUNT };

/* ============ Network seam ============ */

#define SETUP_NET_MAX       8
#define SETUP_SSID_CAP      33

typedef struct {
    char    ssid[SETUP_SSID_CAP];
    uint8_t bars;                     /* 0..3 */
    uint8_t secured;
    uint8_t wired;                    /* 1 = Ethernet entry */
} setup_net_entry_t;

/*
 * setup_network_scan - list networks for the Network page.
 *
 * LEAD WIRING POINT: replace the body with a call into nxnet.h once the
 * Networking area lands. Returns the number of entries written (0..max)
 * or SETUP_ERR_UNSUPPORTED when no network service is available (the
 * page then shows an honest "not available yet" state). The preview
 * build returns a clearly marked demo list.
 */
int setup_network_scan(setup_net_entry_t *out, int max);

/*
 * setup_network_join - join the chosen network. password may be NULL for
 * open networks; it is never stored or logged. LEAD WIRING POINT (nxnet.h).
 */
int setup_network_join(const char *ssid, const char *password, size_t password_len);

/* ============ Accounts seam ============ */

/*
 * setup_accounts_create - create the first (administrator) account.
 *
 * No userspace accounts service or syscall exists yet (see
 * docs/desktop/proposals/setup.md). Returns SETUP_ERR_UNSUPPORTED on the
 * real system; the caller must still wipe the password buffer.
 */
int setup_accounts_create(const char *account, const char *full_name,
                          const char *password, size_t password_len,
                          const char *hint);

/* ============ Model ============ */

typedef struct {
    int language, region, keyboard;
    int keyboard_user_set;

    uint8_t ax[SETUP_AX_COUNT];

    setup_net_entry_t nets[SETUP_NET_MAX];
    int     net_count;                /* < 0: SETUP_ERR_* from scan */
    int     net_sel;                  /* -1 none */
    int     net_joined;               /* index joined, -1 none */
    setup_field_t net_password;
    const char *net_error;            /* inline error, static string */

    setup_field_t full_name, account, password, verify, hint;
    int     account_edited;           /* stop auto-deriving */
    int     account_attempted;        /* Continue pressed: show all errors */

    int     ketivee_mode;             /* 0 sign in, 1 create */
    setup_field_t kid_email, kid_password;
    int     ketivee_attempted;
    int     ketivee_pending;          /* entered, finish online later */

    uint8_t privacy[SETUP_PRIV_COUNT];

    int     appearance;
    int     accent;

    /* Results of the Setting up page */
    int     result_prefs;
    int     result_account;
    int     result_marker;
} setup_model_t;

extern setup_model_t g_setup;

/* ============ Validation (setup_store.c) ============ */

/* Each returns NULL when valid, else a user-facing static message. */
const char *setup_check_full_name(const char *s);
const char *setup_check_account(const char *s);
const char *setup_check_password(const char *pw, size_t len, const char *account, const char *full_name);
const char *setup_check_verify(const setup_field_t *pw, const setup_field_t *verify);
const char *setup_check_hint(const char *hint, const setup_field_t *pw);
const char *setup_check_email(const char *s);

/* 0 = too short, 1 weak, 2 fair, 3 good, 4 strong */
int  setup_password_strength(const char *pw, size_t len, const char *account, const char *full_name);
/* Derive a short account name from a full name (lowercase a-z0-9). */
void setup_derive_account(const char *full_name, char *out, size_t cap);

/* ============ Persistence (setup_store.c) ============ */

int  setup_store_marker_present(void);
int  setup_store_save_prefs(const setup_model_t *m);
int  setup_store_write_marker(const setup_model_t *m);

/* ============ Fields and strings (setup_store.c) ============ */

void setup_secure_wipe(void *p, size_t n);
void setup_field_clear(setup_field_t *f);
int  setup_field_append(setup_field_t *f, char ch);
int  setup_field_backspace(setup_field_t *f);
void setup_field_set(setup_field_t *f, const char *s);
size_t setup_strlen(const char *s);
void setup_strcpy(char *dst, const char *src, size_t cap);
void setup_log(const char *msg);
void setup_log2(const char *a, const char *b);

/* ============ Hit regions and focus (setup_assistant.c) ============ */

typedef enum {
    SETUP_HIT_NONE = 0,
    SETUP_HIT_BACK,
    SETUP_HIT_CONTINUE,
    SETUP_HIT_SECONDARY,              /* "Set Up Later" / "Skip" */
    SETUP_HIT_SLOT                    /* slot + item */
} setup_hit_kind_t;

/* Register a clickable rect for the page being drawn (absolute coords).
 * Ignored while a page transition is running. */
void setup_hit_add(nxc_rect_t r, setup_hit_kind_t kind, int slot, int item);
/* Focused slot on the current page, or -1. */
int  setup_focus_slot(void);
/* 1 when slot is focused and the user is navigating with the keyboard
 * (focus rings on lists/switches; fields always show their caret). */
int  setup_focus_ring_visible(int slot);
/* Pointer position for hover states. */
int  setup_hover(nxc_rect_t r);
/* 1 when "Reduce motion" is on. */
int  setup_reduce_motion(void);

/* ============ Pages (setup_pages.c) ============ */

typedef enum { SETUP_SLOT_FIELD = 0, SETUP_SLOT_LIST, SETUP_SLOT_TOGGLE } setup_slot_kind_t;

typedef struct {
    setup_slot_kind_t kind;
    setup_field_t    *field;          /* FIELD */
    int              *sel;            /* LIST */
    int               count;          /* LIST */
    int               horizontal;     /* LIST: arrows left/right */
    uint8_t          *flag;           /* TOGGLE */
} setup_slot_t;

#define SETUP_MAX_SLOTS 8

int  setup_page_slots(setup_page_t p, setup_slot_t *out, int max);
/* Called after a slot changed (typing, selection, toggle). */
void setup_page_changed(setup_page_t p, int slot);
/* Called once when a page becomes current. */
void setup_page_enter(setup_page_t p);
/* Validate before leaving forward. Returns 1 if the flow may continue. */
int  setup_page_validate(setup_page_t p);
/* Footer button labels. Secondary NULL = hidden. */
const char *setup_page_continue_label(setup_page_t p);
const char *setup_page_secondary_label(setup_page_t p);
/* Secondary button pressed (skip). Returns 1 if the flow should advance. */
int  setup_page_secondary(setup_page_t p);
/* Per-frame logic (Setting up page runs its tasks here). Returns 1 when
 * the page wants the flow to advance by itself. */
int  setup_page_tick(setup_page_t p, float dt);
/* Draw the page body inside the card. */
void setup_page_render(nxc_canvas_t *c, setup_page_t p, nxc_rect_t card);

#endif /* SETUP_INTERNAL_H */
