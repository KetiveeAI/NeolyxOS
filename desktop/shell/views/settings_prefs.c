/*
 * NeolyxOS - System Settings: preference store
 *
 * Validated, audited access to the user .nlc config (nxconfig.h). The
 * config handle is loaded once and kept: nxconfig keeps a small static
 * pool of handles, so loading per access would exhaust it.
 *
 * Validation:
 *   - section/key: 1..63 chars of [a-z0-9_]
 *   - ints: parsed strictly (optional '-', digits only) and range-checked
 *   - choices: only values from the caller's allowed list are stored or
 *     returned; anything else on read falls back to the default
 *
 * Every successful change is logged as
 *   [SETTINGS] config <section>.<key>: <old> -> <new>
 * and every rejected change as "[SETTINGS] config rejected ...".
 *
 * Note: nxc_config_save() is the config layer's persistence point. The
 * layer currently keeps values in memory (its VFS write is a TODO in
 * lib/nxconfig.c), so preferences last for the session until it lands;
 * nothing in this file needs to change when it does.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_internal.h"
#include "nxconfig.h"

static nxc_config_t *g_cfg = 0;
static int g_cfg_tried = 0;

int sprefs_init(void) {
    if (g_cfg) return 0;
    if (g_cfg_tried) return -1;
    g_cfg_tried = 1;
    g_cfg = nxc_config_load(NXC_USER_DEFAULTS);
    if (!g_cfg) {
        settings_log("config: could not load " NXC_USER_DEFAULTS "; preferences are not stored");
        return -1;
    }
    settings_log("config: loaded " NXC_USER_DEFAULTS);
    return 0;
}

int sprefs_available(void) { return g_cfg != 0; }

static int valid_ident(const char *s) {
    if (!s || !s[0]) return 0;
    int n = 0;
    for (; s[n]; n++) {
        char ch = s[n];
        if (n >= NXC_MAX_KEY_LEN - 1) return 0;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) return 0;
    }
    return 1;
}

/* Strict decimal parse. Returns 0 on success. */
static int parse_int(const char *s, int *out) {
    if (!s || !s[0] || !out) return -1;
    int neg = 0, i = 0;
    int64_t v = 0;
    if (s[0] == '-') { neg = 1; i = 1; }
    if (!s[i]) return -1;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
        if (v > 0x7FFFFFFF) return -1;
    }
    *out = (int)(neg ? -v : v);
    return 0;
}

static const char *raw_get(const char *sec, const char *key) {
    if (sprefs_init() != 0) return 0;
    if (!valid_ident(sec) || !valid_ident(key)) return 0;
    return nxc_get_string(g_cfg, sec, key, 0);
}

static void audit(const char *sec, const char *key, const char *old_v, const char *new_v, int saved) {
    char line[200];
    sx_join3(line, (int)sizeof(line), "config ", sec, ".");
    sx_strcat(line, key, (int)sizeof(line));
    sx_strcat(line, ": ", (int)sizeof(line));
    sx_strcat(line, old_v ? old_v : "(unset)", (int)sizeof(line));
    sx_strcat(line, " -> ", (int)sizeof(line));
    sx_strcat(line, new_v, (int)sizeof(line));
    if (!saved) sx_strcat(line, " (save failed)", (int)sizeof(line));
    settings_log(line);
}

static void reject(const char *sec, const char *key, const char *why) {
    char line[200];
    sx_join3(line, (int)sizeof(line), "config rejected ", sec ? sec : "?", ".");
    sx_strcat(line, key ? key : "?", (int)sizeof(line));
    sx_strcat(line, ": ", (int)sizeof(line));
    sx_strcat(line, why, (int)sizeof(line));
    settings_log(line);
}

static int raw_set(const char *sec, const char *key, const char *value) {
    if (!valid_ident(sec) || !valid_ident(key)) { reject(sec, key, "invalid name"); return -1; }
    if (!value || sx_strlen(value) >= NXC_MAX_VALUE_LEN) { reject(sec, key, "invalid value"); return -1; }
    if (sprefs_init() != 0) { reject(sec, key, "config unavailable"); return -1; }
    char old_v[64];
    const char *o = nxc_get_string(g_cfg, sec, key, 0);
    sx_strcpy(old_v, o ? o : "(unset)", (int)sizeof(old_v));
    if (o && sx_streq(o, value)) return 0;          /* no change, nothing to audit */
    if (nxc_set_string(g_cfg, sec, key, value) != 0) { reject(sec, key, "config full"); return -1; }
    int saved = nxc_config_save(g_cfg) == 0;
    audit(sec, key, old_v, value, saved);
    return saved ? 0 : -1;
}

int sprefs_get_int(const char *sec, const char *key, int def, int min, int max) {
    const char *s = raw_get(sec, key);
    int v;
    if (!s || parse_int(s, &v) != 0 || v < min || v > max) return def;
    return v;
}

int sprefs_get_bool(const char *sec, const char *key, int def) {
    const char *s = raw_get(sec, key);
    if (!s) return def ? 1 : 0;
    if (sx_streq(s, "true") || sx_streq(s, "1") || sx_streq(s, "yes")) return 1;
    if (sx_streq(s, "false") || sx_streq(s, "0") || sx_streq(s, "no")) return 0;
    return def ? 1 : 0;
}

int sprefs_get_choice(const char *sec, const char *key, const char *const *allowed, int n, int def) {
    const char *s = raw_get(sec, key);
    if (!s || !allowed || n <= 0) return def;
    for (int i = 0; i < n; i++)
        if (allowed[i] && sx_streq(s, allowed[i])) return i;
    return def;
}

int sprefs_set_int(const char *sec, const char *key, int value, int min, int max) {
    if (value < min || value > max) { reject(sec, key, "out of range"); return -1; }
    char buf[24];
    sx_itoa(value, buf, (int)sizeof(buf));
    return raw_set(sec, key, buf);
}

int sprefs_set_bool(const char *sec, const char *key, int value) {
    return raw_set(sec, key, value ? "true" : "false");
}

int sprefs_set_choice(const char *sec, const char *key, const char *const *allowed, int n, int index) {
    if (!allowed || index < 0 || index >= n || !allowed[index]) { reject(sec, key, "not an allowed value"); return -1; }
    return raw_set(sec, key, allowed[index]);
}
