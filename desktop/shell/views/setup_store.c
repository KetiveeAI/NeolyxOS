/*
 * NeolyxOS - Setup Assistant: validation, tables, persistence, seams
 *
 * Security rules this file enforces:
 *   - Passwords live only in fixed setup_field_t buffers and are wiped
 *     with setup_secure_wipe (volatile stores, not elided by the compiler).
 *   - No password, hint or e-mail value is ever logged. Names are logged
 *     as "(set)"; only non-identifying preferences are logged by value.
 *   - Only non-secret choices are persisted (config layer + marker file).
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "setup_internal.h"
#include "nxconfig.h"
#include "nxsyscall.h"

/* ============ Logging ============ */

void setup_log(const char *msg) {
    if (!msg) return;
    nx_debug_print(msg);
}

void setup_log2(const char *a, const char *b) {
    char line[160];
    size_t n = 0;
    const char *parts[2] = { a, b };
    for (int i = 0; i < 2; i++) {
        const char *s = parts[i];
        while (s && *s && n < sizeof(line) - 2) line[n++] = *s++;
    }
    line[n++] = '\n';
    line[n] = '\0';
    nx_debug_print(line);
}

/* ============ Strings ============ */

size_t setup_strlen(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

void setup_strcpy(char *dst, const char *src, size_t cap) {
    if (!dst || cap == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    }
    dst[i] = '\0';
}

static int s_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* Case-insensitive substring test over explicit lengths. */
static int contains_ci(const char *hay, size_t hn, const char *needle, size_t nn) {
    if (!hay || !needle || nn == 0 || nn > hn) return 0;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t k = 0;
        while (k < nn && lower(hay[i + k]) == lower(needle[k])) k++;
        if (k == nn) return 1;
    }
    return 0;
}

/* ============ Secure wipe and fields ============ */

void setup_secure_wipe(void *p, size_t n) {
    if (!p) return;
    volatile uint8_t *v = (volatile uint8_t *)p;
    for (size_t i = 0; i < n; i++) v[i] = 0;
}

void setup_field_clear(setup_field_t *f) {
    if (!f) return;
    setup_secure_wipe(f->buf, sizeof(f->buf));
    f->len = 0;
    f->touched = 0;
}

int setup_field_append(setup_field_t *f, char ch) {
    if (!f) return SETUP_ERR_INVALID;
    /* Printable 7-bit ASCII only: the keyboard path delivers ASCII and
     * this keeps account names and file content well-formed. */
    if ((unsigned char)ch < 0x20 || (unsigned char)ch > 0x7E) return SETUP_ERR_INVALID;
    if ((size_t)f->len + 1 >= sizeof(f->buf)) return SETUP_ERR_INVALID;
    f->buf[f->len++] = ch;
    f->buf[f->len] = '\0';
    f->touched = 1;
    return SETUP_OK;
}

int setup_field_backspace(setup_field_t *f) {
    if (!f || f->len == 0) return SETUP_ERR_INVALID;
    f->len--;
    f->buf[f->len] = '\0';
    f->touched = 1;
    return SETUP_OK;
}

void setup_field_set(setup_field_t *f, const char *s) {
    if (!f) return;
    setup_secure_wipe(f->buf, sizeof(f->buf));
    setup_strcpy(f->buf, s, sizeof(f->buf));
    f->len = (uint8_t)setup_strlen(f->buf);
}

/* ============ Tables ============ */

const setup_choice_t g_setup_languages[] = {
    { "en_US", "English (US)",          "English" },
    { "en_GB", "English (UK)",          "English" },
    { "en_IN", "English (India)",       "English" },
    { "hi_IN", "Hindi",                 "Hindi" },
    { "es_ES", "Espa\xC3\xB1ol",        "Spanish" },
    { "fr_FR", "Fran\xC3\xA7" "ais",    "French" },
    { "de_DE", "Deutsch",               "German" },
    { "pt_BR", "Portugu\xC3\xAAs (Brasil)", "Portuguese" },
};
const int g_setup_language_count = (int)(sizeof(g_setup_languages) / sizeof(g_setup_languages[0]));

const setup_choice_t g_setup_regions[] = {
    { "US", "United States",  "us" },
    { "IN", "India",          "us" },
    { "GB", "United Kingdom", "gb" },
    { "DE", "Germany",        "de" },
    { "FR", "France",         "fr" },
    { "ES", "Spain",          "es" },
    { "BR", "Brazil",         "br" },
};
const int g_setup_region_count = (int)(sizeof(g_setup_regions) / sizeof(g_setup_regions[0]));

const setup_choice_t g_setup_keyboards[] = {
    { "us",      "US",                "QWERTY" },
    { "us-intl", "US International",  "QWERTY" },
    { "gb",      "British",           "QWERTY" },
    { "de",      "German",            "QWERTZ" },
    { "fr",      "French",            "AZERTY" },
    { "es",      "Spanish",           "QWERTY" },
    { "br",      "Brazilian",         "ABNT2" },
};
const int g_setup_keyboard_count = (int)(sizeof(g_setup_keyboards) / sizeof(g_setup_keyboards[0]));

const char *const g_setup_hello[] = {
    "Hello", "Namaste", "Hola", "Bonjour", "Hallo", "Ciao",
    "Ol\xC3\xA1", "Konnichiwa", "Ni hao", "Salaam", "Hej", "Merhaba",
};
const int g_setup_hello_count = (int)(sizeof(g_setup_hello) / sizeof(g_setup_hello[0]));

const setup_accent_t g_setup_accents[] = {
    { "Saffron",  NXC_RGB(0xE8, 0x66, 0x0A) },
    { "Blue",     NXC_RGB(0x0A, 0x84, 0xFF) },
    { "Teal",     NXC_RGB(0x00, 0xB4, 0xD8) },
    { "Green",    NXC_RGB(0x30, 0xB0, 0x50) },
    { "Purple",   NXC_RGB(0x8E, 0x5B, 0xE8) },
    { "Pink",     NXC_RGB(0xE8, 0x4A, 0x8A) },
    { "Red",      NXC_RGB(0xE5, 0x48, 0x3E) },
    { "Graphite", NXC_RGB(0x8A, 0x8E, 0x96) },
};
const int g_setup_accent_count = (int)(sizeof(g_setup_accents) / sizeof(g_setup_accents[0]));

/* ============ Validation ============ */

const char *setup_check_full_name(const char *s) {
    size_t n = setup_strlen(s);
    if (n == 0) return "Enter your full name.";
    int letters = 0;
    for (size_t i = 0; i < n; i++) {
        char ch = s[i];
        if ((unsigned char)ch < 0x20 || (unsigned char)ch > 0x7E) return "Use letters, spaces and simple punctuation.";
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) letters++;
    }
    if (letters == 0) return "Your name needs at least one letter.";
    return 0;
}

static const char *const k_reserved_accounts[] = {
    "root", "admin", "administrator", "system", "guest", "daemon",
    "nobody", "kernel", "neolyx", "setup", "user", "users",
};

const char *setup_check_account(const char *s) {
    size_t n = setup_strlen(s);
    if (n == 0) return "Enter an account name.";
    if (n > 31) return "Use 31 characters or fewer.";
    if (!(s[0] >= 'a' && s[0] <= 'z')) return "Start with a lowercase letter.";
    for (size_t i = 0; i < n; i++) {
        char ch = s[i];
        int ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                 ch == '.' || ch == '_' || ch == '-';
        if (!ok) return "Use lowercase letters, numbers, . _ or -";
    }
    for (size_t i = 0; i < sizeof(k_reserved_accounts) / sizeof(k_reserved_accounts[0]); i++)
        if (s_eq(s, k_reserved_accounts[i])) return "That name is reserved by the system.";
    return 0;
}

static const char *const k_common_passwords[] = {
    "password", "password1", "12345678", "123456789", "1234567890", "qwerty123",
    "qwertyuiop", "11111111", "iloveyou", "letmein1", "neolyxos", "admin123",
};

static int is_common(const char *pw, size_t len) {
    for (size_t i = 0; i < sizeof(k_common_passwords) / sizeof(k_common_passwords[0]); i++) {
        const char *c = k_common_passwords[i];
        size_t cn = setup_strlen(c);
        if (cn == len && contains_ci(pw, len, c, cn)) return 1;
    }
    return 0;
}

int setup_password_strength(const char *pw, size_t len, const char *account, const char *full_name) {
    if (!pw || len < 8) return 0;
    if (is_common(pw, len)) return 1;
    int lo = 0, up = 0, dg = 0, sy = 0;
    for (size_t i = 0; i < len; i++) {
        char ch = pw[i];
        if (ch >= 'a' && ch <= 'z') lo = 1;
        else if (ch >= 'A' && ch <= 'Z') up = 1;
        else if (ch >= '0' && ch <= '9') dg = 1;
        else sy = 1;
    }
    int classes = lo + up + dg + sy;
    int score = 1;
    if (len >= 10 && classes >= 2) score++;
    if (len >= 12 && classes >= 3) score++;
    if ((len >= 14 && classes >= 3) || (len >= 20 && classes >= 2)) score++;
    if (classes == 1 && len < 16) score = 1;
    /* Containing the account name or a name part makes it guessable. */
    size_t an = setup_strlen(account);
    if (an >= 3 && contains_ci(pw, len, account, an)) score = 1;
    if (full_name) {
        const char *p = full_name;
        while (*p) {
            while (*p == ' ') p++;
            const char *w = p;
            while (*p && *p != ' ') p++;
            size_t wn = (size_t)(p - w);
            if (wn >= 3 && contains_ci(pw, len, w, wn)) score = 1;
        }
    }
    if (score > 4) score = 4;
    return score;
}

const char *setup_check_password(const char *pw, size_t len, const char *account, const char *full_name) {
    if (!pw || len == 0) return "Enter a password.";
    if (len < 8) return "Use at least 8 characters.";
    if (is_common(pw, len)) return "This password is too common.";
    if (setup_password_strength(pw, len, account, full_name) < 2)
        return "Add length, numbers or symbols, and avoid your name.";
    return 0;
}

const char *setup_check_verify(const setup_field_t *pw, const setup_field_t *verify) {
    if (!pw || !verify) return "Re-enter your password.";
    if (verify->len == 0) return "Re-enter your password.";
    if (verify->len != pw->len) return "The passwords don't match.";
    /* Constant-time compare over the fixed length. */
    uint8_t diff = 0;
    for (size_t i = 0; i < pw->len; i++) diff |= (uint8_t)(pw->buf[i] ^ verify->buf[i]);
    return diff ? "The passwords don't match." : 0;
}

const char *setup_check_hint(const char *hint, const setup_field_t *pw) {
    size_t hn = setup_strlen(hint);
    if (hn == 0 || !pw || pw->len == 0) return 0;
    if (contains_ci(hint, hn, pw->buf, pw->len)) return "The hint can't contain your password.";
    return 0;
}

const char *setup_check_email(const char *s) {
    size_t n = setup_strlen(s);
    if (n == 0) return "Enter your e-mail address.";
    size_t at = 0, ats = 0;
    for (size_t i = 0; i < n; i++) {
        char ch = s[i];
        if (ch == ' ') return "E-mail addresses can't contain spaces.";
        if (ch == '@') { ats++; at = i; }
    }
    if (ats != 1 || at == 0 || at + 1 >= n) return "Enter a valid e-mail address.";
    int dot = 0;
    for (size_t i = at + 2; i + 1 < n; i++) if (s[i] == '.') dot = 1;
    if (!dot || s[n - 1] == '.') return "Enter a valid e-mail address.";
    return 0;
}

void setup_derive_account(const char *full_name, char *out, size_t cap) {
    if (!out || cap == 0) return;
    size_t n = 0;
    if (full_name) {
        for (const char *p = full_name; *p && n + 1 < cap && n < 31; p++) {
            char ch = lower(*p);
            if ((ch >= 'a' && ch <= 'z') || (n > 0 && ch >= '0' && ch <= '9')) out[n++] = ch;
        }
    }
    out[n] = '\0';
}

/* ============ Network seam ============ */

int setup_network_scan(setup_net_entry_t *out, int max) {
    if (!out || max <= 0) return SETUP_ERR_INVALID;
#ifdef NX_HOST_PREVIEW
    /* PREVIEW DEMO DATA ONLY - not compiled into the real system. */
    static const setup_net_entry_t demo[] = {
        { "Ethernet",        3, 0, 1 },
        { "Ketivee Lab 5G",  3, 1, 0 },
        { "Ketivee Guest",   2, 0, 0 },
        { "Pharmacy-WLAN",   1, 1, 0 },
    };
    int n = (int)(sizeof(demo) / sizeof(demo[0]));
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = demo[i];
    setup_log("[SETUP] network scan: preview demo list\n");
    return n;
#else
    /* LEAD: wire to nxnet.h (Networking area). Until then: honest state. */
    setup_log("[SETUP] network scan: no network service (unsupported)\n");
    return SETUP_ERR_UNSUPPORTED;
#endif
}

int setup_network_join(const char *ssid, const char *password, size_t password_len) {
    if (!ssid || !ssid[0]) return SETUP_ERR_INVALID;
    if (password_len > 0 && !password) return SETUP_ERR_INVALID;
#ifdef NX_HOST_PREVIEW
    /* PREVIEW DEMO ONLY: pretend the join worked. Password not used. */
    (void)password;
    setup_log("[SETUP] network join: preview demo success\n");
    return SETUP_OK;
#else
    (void)password;
    setup_log("[SETUP] network join: no network service (unsupported)\n");
    return SETUP_ERR_UNSUPPORTED;
#endif
}

/* ============ Accounts seam ============ */

int setup_accounts_create(const char *account, const char *full_name,
                          const char *password, size_t password_len,
                          const char *hint) {
    if (!account || !full_name || !password || password_len == 0 || password_len >= SETUP_FIELD_CAP)
        return SETUP_ERR_INVALID;
    if (setup_check_account(account) || setup_check_full_name(full_name)) return SETUP_ERR_INVALID;
    (void)hint;
    /* No accounts syscall exists (docs/desktop/proposals/setup.md). The
     * password is not hashed or stored here: a userspace hash with no
     * protected store would be weaker than the proposed kernel service. */
    setup_log("[SETUP] account create: accounts service unavailable (unsupported)\n");
    return SETUP_ERR_UNSUPPORTED;
}

/* ============ Persistence ============ */

#define SETUP_MARKER_PATH "/System/Config/setup_complete.nlc"

int setup_store_marker_present(void) {
    int fd = nx_open(SETUP_MARKER_PATH, O_RDONLY);
    if (fd < 0) return 0;
    nx_close(fd);
    return 1;
}

static nxc_config_t *g_cfg;

static nxc_config_t *store_config(void) {
    /* nxconfig keeps a small static pool and never frees handles, so
     * load once and keep the handle for the life of the shell. */
    if (!g_cfg) g_cfg = nxc_config_load(NXC_SYSTEM_CONFIG);
    if (!g_cfg) setup_log("[SETUP] ERROR: could not open system config\n");
    return g_cfg;
}

static int set_logged(nxc_config_t *cfg, const char *sec, const char *key, const char *val, int redact) {
    if (nxc_set_string(cfg, sec, key, val) != 0) {
        setup_log2("[SETUP] ERROR: config write failed: ", key);
        return SETUP_ERR_IO;
    }
    char what[128];
    size_t n = 0;
    const char *parts[5] = { sec, ".", key, " = ", redact ? "(set, value not logged)" : val };
    for (int i = 0; i < 5; i++)
        for (const char *p = parts[i]; p && *p && n < sizeof(what) - 1; p++) what[n++] = *p;
    what[n] = '\0';
    setup_log2("[SETUP] config ", what);
    return SETUP_OK;
}

static const char *appearance_name(int a) {
    return a == SETUP_APPEAR_LIGHT ? "light" : a == SETUP_APPEAR_AUTO ? "auto" : "dark";
}

static int clamp_index(int v, int count) { return (v < 0 || v >= count) ? 0 : v; }

int setup_store_save_prefs(const setup_model_t *m) {
    if (!m) return SETUP_ERR_INVALID;
    nxc_config_t *cfg = store_config();
    if (!cfg) return SETUP_ERR_IO;
    int rc = SETUP_OK;
    static const char *const ax_keys[SETUP_AX_COUNT] = {
        "larger_text", "bold_text", "reduce_motion", "increase_contrast", "reduce_transparency" };
    static const char *const priv_keys[SETUP_PRIV_COUNT] = {
        "share_analytics", "share_crash_reports", "personalized_suggestions", "location_services" };

    if (set_logged(cfg, "locale", "language", g_setup_languages[clamp_index(m->language, g_setup_language_count)].code, 0)) rc = SETUP_ERR_IO;
    if (set_logged(cfg, "locale", "region", g_setup_regions[clamp_index(m->region, g_setup_region_count)].code, 0)) rc = SETUP_ERR_IO;
    if (set_logged(cfg, "locale", "keyboard", g_setup_keyboards[clamp_index(m->keyboard, g_setup_keyboard_count)].code, 0)) rc = SETUP_ERR_IO;
    for (int i = 0; i < SETUP_AX_COUNT; i++)
        if (set_logged(cfg, "accessibility", ax_keys[i], m->ax[i] ? "true" : "false", 0)) rc = SETUP_ERR_IO;
    for (int i = 0; i < SETUP_PRIV_COUNT; i++)
        if (set_logged(cfg, "privacy", priv_keys[i], m->privacy[i] ? "true" : "false", 0)) rc = SETUP_ERR_IO;
    if (set_logged(cfg, "desktop", "theme", appearance_name(m->appearance), 0)) rc = SETUP_ERR_IO;
    if (set_logged(cfg, "desktop", "accent", g_setup_accents[clamp_index(m->accent, g_setup_accent_count)].name, 0)) rc = SETUP_ERR_IO;
    if (nxc_set_color(cfg, "desktop", "accent_color",
                      g_setup_accents[clamp_index(m->accent, g_setup_accent_count)].color & 0x00FFFFFFu) != 0) rc = SETUP_ERR_IO;
    if (set_logged(cfg, "account", "full_name", m->full_name.buf, 1)) rc = SETUP_ERR_IO;
    if (set_logged(cfg, "account", "name", m->account.buf, 1)) rc = SETUP_ERR_IO;
    if (set_logged(cfg, "account", "pending_creation", m->result_account == SETUP_OK ? "false" : "true", 0)) rc = SETUP_ERR_IO;
    if (m->ketivee_pending && set_logged(cfg, "ketivee", "pending_email", m->kid_email.buf, 1)) rc = SETUP_ERR_IO;
    if (m->net_joined >= 0 && m->net_joined < m->net_count &&
        set_logged(cfg, "network", "preferred", m->nets[m->net_joined].ssid, 1)) rc = SETUP_ERR_IO;
    if (set_logged(cfg, "setup", "completed", "true", 0)) rc = SETUP_ERR_IO;

    if (nxc_config_save(cfg) != 0) {
        setup_log("[SETUP] ERROR: config save failed\n");
        rc = SETUP_ERR_IO;
    } else {
        setup_log("[SETUP] config saved\n");
    }
    return rc;
}

/* Bounded append into the marker buffer. */
typedef struct { char *buf; size_t cap, len; int overflow; } sb_t;

static void sb_add(sb_t *b, const char *s) {
    if (!b || !s) return;
    while (*s) {
        if (b->len + 1 >= b->cap) { b->overflow = 1; return; }
        b->buf[b->len++] = *s++;
    }
    b->buf[b->len] = '\0';
}

static void sb_kv(sb_t *b, const char *k, const char *v) {
    sb_add(b, k); sb_add(b, " = "); sb_add(b, v); sb_add(b, "\n");
}

int setup_store_write_marker(const setup_model_t *m) {
    if (!m) return SETUP_ERR_INVALID;
    /* The marker carries a copy of the non-secret choices so they survive
     * even while nxc_config_save has no VFS backend. Names are included
     * (they are not secrets); passwords, hints and e-mail never are. */
    static char text[1024];
    sb_t b = { text, sizeof(text), 0, 0 };
    text[0] = '\0';
    sb_add(&b, "# NeolyxOS first-run setup marker. Delete to run Setup again.\n[setup]\n");
    sb_kv(&b, "completed", "true");
    sb_kv(&b, "version", "1");
    sb_add(&b, "[locale]\n");
    sb_kv(&b, "language", g_setup_languages[clamp_index(m->language, g_setup_language_count)].code);
    sb_kv(&b, "region", g_setup_regions[clamp_index(m->region, g_setup_region_count)].code);
    sb_kv(&b, "keyboard", g_setup_keyboards[clamp_index(m->keyboard, g_setup_keyboard_count)].code);
    sb_add(&b, "[desktop]\n");
    sb_kv(&b, "theme", appearance_name(m->appearance));
    sb_kv(&b, "accent", g_setup_accents[clamp_index(m->accent, g_setup_accent_count)].name);
    sb_add(&b, "[account]\n");
    sb_kv(&b, "full_name", m->full_name.buf);
    sb_kv(&b, "name", m->account.buf);
    sb_kv(&b, "pending_creation", m->result_account == SETUP_OK ? "false" : "true");
    sb_add(&b, "[privacy]\n");
    sb_kv(&b, "share_analytics", m->privacy[SETUP_PRIV_ANALYTICS] ? "true" : "false");
    sb_kv(&b, "share_crash_reports", m->privacy[SETUP_PRIV_CRASH_REPORTS] ? "true" : "false");
    sb_kv(&b, "personalized_suggestions", m->privacy[SETUP_PRIV_SUGGESTIONS] ? "true" : "false");
    sb_kv(&b, "location_services", m->privacy[SETUP_PRIV_LOCATION] ? "true" : "false");
    if (b.overflow) {
        setup_log("[SETUP] ERROR: marker text overflow\n");
        return SETUP_ERR_IO;
    }

    int fd = nx_open(SETUP_MARKER_PATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        setup_log("[SETUP] ERROR: cannot create " SETUP_MARKER_PATH "\n");
        return SETUP_ERR_IO;
    }
    int64_t w = nx_write(fd, text, (uint64_t)b.len);
    int cr = nx_close(fd);
    if (w != (int64_t)b.len || cr < 0) {
        setup_log("[SETUP] ERROR: marker write incomplete\n");
        return SETUP_ERR_IO;
    }
    setup_log("[SETUP] setup-completed marker written: " SETUP_MARKER_PATH "\n");
    return SETUP_OK;
}
