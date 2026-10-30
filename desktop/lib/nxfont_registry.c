/*
 * NeolyxOS - Installed font registry (see nxfont_registry.h)
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../include/nxfont_registry.h"
#include "../include/nxsyscall.h"

/* ============ State ============ */

typedef struct {
    nxfont_entry_t     e;
    const nxc_font_t  *face;        /* system: caller's face; user: owned */
    nxc_font_t        *owned;       /* user faces we must destroy */
    uint8_t           *data;        /* user font file bytes (owned) */
    uint32_t           data_size;
    uint32_t           char_count;  /* 0 = not computed yet */
    char               file[64];    /* user font: bare file name in the dir */
} slot_t;

static struct {
    slot_t   slots[NXFONT_MAX_FONTS];
    uint32_t count;
    uint32_t next_id;
    char     dir[NXFONT_PATH_MAX];
} g_reg;

/* ============ Small string helpers (freestanding) ============ */

static uint32_t s_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

/* Copy with truncation; dst always NUL-terminated (cap >= 1). */
static void s_copy(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void s_cat(char *dst, uint32_t cap, const char *src) {
    uint32_t n = s_len(dst);
    if (n >= cap) return;
    s_copy(dst + n, cap - n, src);
}

static void s_cat_u32(char *dst, uint32_t cap, uint32_t v) {
    char tmp[12];
    uint32_t n = 0;
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11);
    char out[12];
    for (uint32_t i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    s_cat(dst, cap, out);
}

static void s_cat_i64(char *dst, uint32_t cap, int64_t v) {
    if (v < 0) { s_cat(dst, cap, "-"); v = -v; }
    s_cat_u32(dst, cap, (uint32_t)(v > 0xFFFFFFFF ? 0xFFFFFFFF : v));
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int s_eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (lower(*a) != lower(*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int s_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int ends_with_ci(const char *s, const char *suffix) {
    uint32_t n = s_len(s), m = s_len(suffix);
    return n >= m && s_eq_ci(s + n - m, suffix);
}

static void reg_log(const char *a, const char *b, const char *c) {
    char line[320];
    line[0] = '\0';
    s_cat(line, sizeof(line), "[FONTS] ");
    s_cat(line, sizeof(line), a);
    if (b) s_cat(line, sizeof(line), b);
    if (c) s_cat(line, sizeof(line), c);
    s_cat(line, sizeof(line), "\n");
    nx_debug_print(line);
}

static void set_reason(char *reason, uint32_t cap, const char *a, const char *b) {
    if (!reason || cap == 0) return;
    s_copy(reason, cap, a);
    if (b) s_cat(reason, cap, b);
}

/* ============ Path validation ============ */

/* Bare file name: 1..63 chars of [A-Za-z0-9._-], not starting with '.',
 * ending in ".ttf". */
static int file_name_ok(const char *n) {
    uint32_t len = s_len(n);
    if (len < 5 || len > 63 || n[0] == '.') return 0;
    for (uint32_t i = 0; i < len; i++) {
        char c = n[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '.' || c == '_' || c == '-';
        if (!ok) return 0;
    }
    return ends_with_ci(n, ".ttf");
}

/* Absolute path of printable ASCII without "." / ".." components or
 * doubled slashes, shorter than NXFONT_PATH_MAX. */
static int path_ok(const char *p) {
    uint32_t len = s_len(p);
    if (len < 2 || len >= NXFONT_PATH_MAX || p[0] != '/') return 0;
    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c > 0x7E || c == '\\') return 0;
        if (c == '/') {
            if (p[i + 1] == '/') return 0;
            if (p[i + 1] == '.' && (p[i + 2] == '/' || p[i + 2] == 0)) return 0;
            if (p[i + 1] == '.' && p[i + 2] == '.' && (p[i + 3] == '/' || p[i + 3] == 0)) return 0;
        }
    }
    return 1;
}

static const char *base_name(const char *p) {
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/') b = q + 1;
    return b;
}

/* ============ File I/O through the syscall wrappers ============ */

/*
 * Read a whole file into a new nx_alloc buffer. Size comes from seeking
 * to the end; reads loop until the size is reached. Returns NXC_OK and
 * the buffer, or an error with a reason.
 */
static nxc_status_t read_file(const char *path, uint32_t min_size, uint32_t max_size,
                              uint8_t **out, uint32_t *size_out, char *reason, uint32_t cap) {
    *out = 0;
    *size_out = 0;
    int fd = nx_open(path, O_RDONLY);
    if (fd < 0) {
        if (fd == -2) {
            set_reason(reason, cap, "file not found", 0);
        } else {
            set_reason(reason, cap, "cannot open file (error ", 0);
            s_cat_i64(reason, cap, fd);
            s_cat(reason, cap, ")");
        }
        return NXC_ERR_NOT_FOUND;
    }
    int64_t end = nx_seek(fd, 0, SEEK_END);
    if (end < 0 || nx_seek(fd, 0, SEEK_SET) != 0) {
        nx_close(fd);
        set_reason(reason, cap, "cannot determine the file size", 0);
        return NXC_ERR_UNSUPPORTED;
    }
    if ((uint64_t)end < min_size || (uint64_t)end > max_size) {
        nx_close(fd);
        set_reason(reason, cap, (uint64_t)end < min_size ? "file is too small to be a font"
                                                         : "file is larger than the 8 MiB limit", 0);
        return NXC_ERR_BAD_FONT;
    }
    uint32_t size = (uint32_t)end;
    uint8_t *buf = (uint8_t *)nx_alloc((uint64_t)size + 16u);
    if (!buf) {
        nx_close(fd);
        set_reason(reason, cap, "out of memory", 0);
        return NXC_ERR_NO_MEMORY;
    }
    uint32_t got = 0;
    uint32_t guard = 0;
    while (got < size && guard++ < 1u << 16) {
        int64_t n = nx_read(fd, buf + got, (uint64_t)(size - got));
        if (n <= 0) break;
        if ((uint64_t)n > (uint64_t)(size - got)) { got = 0xFFFFFFFFu; break; }   /* kernel lied */
        got += (uint32_t)n;
    }
    nx_close(fd);
    if (got != size) {
        nx_free(buf);
        set_reason(reason, cap, "short read", 0);
        return NXC_ERR_BAD_FONT;
    }
    *out = buf;
    *size_out = size;
    return NXC_OK;
}

static nxc_status_t write_file(const char *path, const void *data, uint32_t size) {
    int fd = nx_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return NXC_ERR_NOT_FOUND;
    uint32_t done = 0, guard = 0;
    while (done < size && guard++ < 1u << 16) {
        int64_t n = nx_write(fd, (const uint8_t *)data + done, (uint64_t)(size - done));
        if (n <= 0 || (uint64_t)n > (uint64_t)(size - done)) break;
        done += (uint32_t)n;
    }
    nx_close(fd);
    return done == size ? NXC_OK : NXC_ERR_STATE;
}

/* ============ Validation ============ */

/*
 * Parse font bytes and apply registry policy on top of the engine's
 * structural validation: the face must map at least one printable code
 * point and carry a family name or have a usable file name.
 */
static nxc_status_t validate_bytes(const uint8_t *data, uint32_t size, nxc_font_t **face_out,
                                   nxc_font_info_t *info, char *reason, uint32_t cap) {
    *face_out = 0;
    nxc_font_t *f = 0;
    nxc_status_t st = nxc_font_load(&f, data, size);
    if (st != NXC_OK) {
        const char *why = nxc_font_last_reject();
        set_reason(reason, cap, "invalid font file: ",
                   (st == NXC_ERR_BAD_FONT && why && why[0]) ? why : nxc_status_str(st));
        return st;
    }
    if (nxc_font_info(f, info) != NXC_OK) {
        nxc_font_destroy(f);
        set_reason(reason, cap, "font information unreadable", 0);
        return NXC_ERR_BAD_FONT;
    }
    if (nxc_font_next_char(f, 0x21u) == NXC_NO_CHAR) {
        nxc_font_destroy(f);
        set_reason(reason, cap, "font maps no printable characters", 0);
        return NXC_ERR_BAD_FONT;
    }
    *face_out = f;
    return NXC_OK;
}

nxc_status_t nxfont_validate_file(const char *path, nxc_font_info_t *info, char *reason,
                                  uint32_t reason_cap) {
    if (reason && reason_cap) reason[0] = '\0';
    if (!path || !info) {
        set_reason(reason, reason_cap, "invalid argument", 0);
        return NXC_ERR_INVALID_ARG;
    }
    if (!path_ok(path)) {
        set_reason(reason, reason_cap, "path must be absolute, printable and without . or .. parts", 0);
        return NXC_ERR_INVALID_ARG;
    }
    if (!ends_with_ci(path, ".ttf")) {
        set_reason(reason, reason_cap, "only TrueType (.ttf) files are supported", 0);
        return NXC_ERR_UNSUPPORTED;
    }
    uint8_t *data = 0;
    uint32_t size = 0;
    nxc_status_t st = read_file(path, NXFONT_MIN_FILE_BYTES, NXFONT_MAX_FILE_BYTES, &data, &size,
                                reason, reason_cap);
    if (st != NXC_OK) return st;
    nxc_font_t *f = 0;
    st = validate_bytes(data, size, &f, info, reason, reason_cap);
    if (f) nxc_font_destroy(f);
    nx_free(data);
    return st;
}

/* ============ Registry core ============ */

static slot_t *slot_by_id(uint32_t id) {
    if (id == 0) return 0;
    for (uint32_t i = 0; i < g_reg.count; i++) {
        if (g_reg.slots[i].e.id == id) return &g_reg.slots[i];
    }
    return 0;
}

static slot_t *slot_new(void) {
    if (g_reg.count >= NXFONT_MAX_FONTS) return 0;
    slot_t *s = &g_reg.slots[g_reg.count];
    uint8_t *p = (uint8_t *)s;
    for (uint32_t i = 0; i < sizeof(*s); i++) p[i] = 0;
    s->e.id = ++g_reg.next_id;
    if (s->e.id == 0) s->e.id = ++g_reg.next_id;
    return s;
}

static uint32_t user_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_reg.count; i++) n += g_reg.slots[i].e.source == NXFONT_SOURCE_USER;
    return n;
}

/* Same family, style and version already present. */
static const slot_t *find_duplicate(const nxc_font_info_t *in) {
    for (uint32_t i = 0; i < g_reg.count; i++) {
        const nxc_font_info_t *o = &g_reg.slots[i].e.info;
        if (s_eq(o->family, in->family) && s_eq(o->style, in->style) && s_eq(o->version, in->version) &&
            o->num_glyphs == in->num_glyphs) {
            return &g_reg.slots[i];
        }
    }
    return 0;
}

nxc_status_t nxfont_registry_init(void) {
    for (uint32_t i = 0; i < g_reg.count; i++) {
        slot_t *s = &g_reg.slots[i];
        if (s->owned) nxc_font_destroy(s->owned);
        if (s->data) nx_free(s->data);
    }
    uint8_t *p = (uint8_t *)&g_reg;
    for (uint32_t i = 0; i < sizeof(g_reg); i++) p[i] = 0;
    s_copy(g_reg.dir, sizeof(g_reg.dir), NXFONT_USER_DIR);
    reg_log("registry initialized", 0, 0);
    return NXC_OK;
}

nxc_status_t nxfont_registry_add_system(const nxc_font_t *face, const char *family,
                                        const char *style, const char *role, uint32_t *id_out) {
    if (id_out) *id_out = 0;
    if (!face || !family || !family[0] || !style || !style[0]) return NXC_ERR_INVALID_ARG;
    nxc_font_info_t info;
    if (nxc_font_info(face, &info) != NXC_OK) return NXC_ERR_INVALID_ARG;
    slot_t *s = slot_new();
    if (!s) {
        reg_log("registry full, system font not registered: ", family, 0);
        return NXC_ERR_OVERFLOW;
    }
    s->e.info = info;
    s_copy(s->e.family, sizeof(s->e.family), family);
    s_copy(s->e.style, sizeof(s->e.style), style);
    s_copy(s->e.role, sizeof(s->e.role), role ? role : "system");
    s_copy(s->e.location, sizeof(s->e.location), "Built in");
    s->e.source = NXFONT_SOURCE_SYSTEM;
    s->e.enabled = 1;
    s->e.locked = 1;
    s->e.persisted = 1;
    s->face = face;
    g_reg.count++;
    if (id_out) *id_out = s->e.id;
    reg_log("system font registered: ", family, role ? "" : 0);
    return NXC_OK;
}

/* Register validated user font bytes (takes ownership of data and face). */
static slot_t *add_user(uint8_t *data, uint32_t size, nxc_font_t *face, const nxc_font_info_t *info,
                        const char *file, const char *location, int enabled, int persisted) {
    slot_t *s = slot_new();
    if (!s) return 0;
    s->e.info = *info;
    s_copy(s->e.family, sizeof(s->e.family), info->family[0] ? info->family : file);
    s_copy(s->e.style, sizeof(s->e.style), info->style[0] ? info->style : "Regular");
    s_copy(s->e.role, sizeof(s->e.role), "user");
    s_copy(s->e.location, sizeof(s->e.location), location);
    s->e.source = NXFONT_SOURCE_USER;
    s->e.enabled = (uint8_t)(enabled != 0);
    s->e.locked = 0;
    s->e.persisted = (uint8_t)(persisted != 0);
    s->face = face;
    s->owned = face;
    s->data = data;
    s->data_size = size;
    s_copy(s->file, sizeof(s->file), file);
    g_reg.count++;
    return s;
}

/* Rewrite the index from the registry. */
static nxc_status_t write_index(void) {
    char buf[NXFONT_INDEX_MAX];
    buf[0] = '\0';
    s_cat(buf, sizeof(buf), "# NeolyxOS user fonts: <enabled 0|1> <file>\n");
    for (uint32_t i = 0; i < g_reg.count; i++) {
        const slot_t *s = &g_reg.slots[i];
        if (s->e.source != NXFONT_SOURCE_USER || !s->e.persisted) continue;
        s_cat(buf, sizeof(buf), s->e.enabled ? "1 " : "0 ");
        s_cat(buf, sizeof(buf), s->file);
        s_cat(buf, sizeof(buf), "\n");
    }
    char path[NXFONT_PATH_MAX];
    s_copy(path, sizeof(path), g_reg.dir);
    s_cat(path, sizeof(path), "/" NXFONT_INDEX_NAME);
    nxc_status_t st = write_file(path, buf, s_len(buf));
    reg_log(st == NXC_OK ? "index written: " : "index write failed: ", path, 0);
    return st;
}

nxc_status_t nxfont_registry_load_user(const char *dir, uint32_t *loaded_out) {
    if (loaded_out) *loaded_out = 0;
    if (dir) {
        if (!path_ok(dir)) {
            reg_log("user font directory rejected: ", dir, 0);
            return NXC_ERR_INVALID_ARG;
        }
        s_copy(g_reg.dir, sizeof(g_reg.dir), dir);
    }
    char path[NXFONT_PATH_MAX];
    s_copy(path, sizeof(path), g_reg.dir);
    s_cat(path, sizeof(path), "/" NXFONT_INDEX_NAME);
    char reason[NXFONT_REASON_MAX];
    uint8_t *idx = 0;
    uint32_t idx_size = 0;
    nxc_status_t st = read_file(path, 1, NXFONT_INDEX_MAX, &idx, &idx_size, reason, sizeof(reason));
    if (st != NXC_OK) {
        reg_log("no user font index (", reason, ")");
        return NXC_ERR_NOT_FOUND;
    }
    uint32_t loaded = 0, pos = 0, lines = 0;
    while (pos < idx_size && lines++ < 256) {
        /* One line into a bounded buffer. */
        char line[96];
        uint32_t n = 0;
        int too_long = 0;
        while (pos < idx_size && idx[pos] != '\n') {
            if (n + 1 < sizeof(line)) line[n++] = (char)idx[pos]; else too_long = 1;
            pos++;
        }
        pos++;
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) n--;
        line[n] = '\0';
        if (n == 0 || line[0] == '#') continue;
        if (too_long || n < 3 || (line[0] != '0' && line[0] != '1') || line[1] != ' ') {
            reg_log("index line ignored (malformed)", 0, 0);
            continue;
        }
        const char *file = line + 2;
        if (!file_name_ok(file)) {
            reg_log("index entry rejected (bad file name): ", file, 0);
            continue;
        }
        if (user_count() >= NXFONT_MAX_USER_FONTS || g_reg.count >= NXFONT_MAX_FONTS) {
            reg_log("user font limit reached, skipping: ", file, 0);
            continue;
        }
        char fpath[NXFONT_PATH_MAX];
        s_copy(fpath, sizeof(fpath), g_reg.dir);
        s_cat(fpath, sizeof(fpath), "/");
        s_cat(fpath, sizeof(fpath), file);
        uint8_t *data = 0;
        uint32_t size = 0;
        if (read_file(fpath, NXFONT_MIN_FILE_BYTES, NXFONT_MAX_FILE_BYTES, &data, &size, reason,
                      sizeof(reason)) != NXC_OK) {
            reg_log(file, " rejected: ", reason);
            continue;
        }
        nxc_font_t *face = 0;
        nxc_font_info_t info;
        if (validate_bytes(data, size, &face, &info, reason, sizeof(reason)) != NXC_OK) {
            reg_log(file, " rejected: ", reason);
            nx_free(data);
            continue;
        }
        if (find_duplicate(&info)) {
            reg_log(file, " skipped: duplicate of an installed font", 0);
            nxc_font_destroy(face);
            nx_free(data);
            continue;
        }
        if (!add_user(data, size, face, &info, file, fpath, line[0] == '1', 1)) {
            nxc_font_destroy(face);
            nx_free(data);
            continue;
        }
        loaded++;
        reg_log("user font loaded: ", fpath, line[0] == '1' ? "" : " (disabled)");
    }
    nx_free(idx);
    if (loaded_out) *loaded_out = loaded;
    return NXC_OK;
}

nxc_status_t nxfont_install(const char *path, uint32_t *id_out, char *message, uint32_t cap) {
    if (id_out) *id_out = 0;
    if (message && cap) message[0] = '\0';
    if (!path) {
        set_reason(message, cap, "no path given", 0);
        return NXC_ERR_INVALID_ARG;
    }
    reg_log("install requested: ", path, 0);
    if (!path_ok(path)) {
        set_reason(message, cap, "Path must be absolute, printable and without . or .. parts", 0);
        reg_log("install rejected: bad path", 0, 0);
        return NXC_ERR_INVALID_ARG;
    }
    const char *file = base_name(path);
    if (!file_name_ok(file)) {
        set_reason(message, cap, "File name must be a .ttf name of letters, digits, '.', '_' or '-'", 0);
        reg_log("install rejected: bad file name", 0, 0);
        return NXC_ERR_INVALID_ARG;
    }
    if (user_count() >= NXFONT_MAX_USER_FONTS || g_reg.count >= NXFONT_MAX_FONTS) {
        set_reason(message, cap, "Too many fonts installed", 0);
        return NXC_ERR_OVERFLOW;
    }
    char reason[NXFONT_REASON_MAX];
    uint8_t *data = 0;
    uint32_t size = 0;
    nxc_status_t st = read_file(path, NXFONT_MIN_FILE_BYTES, NXFONT_MAX_FILE_BYTES, &data, &size,
                                reason, sizeof(reason));
    if (st != NXC_OK) {
        set_reason(message, cap, "Cannot install: ", reason);
        reg_log("install rejected: ", reason, 0);
        return st;
    }
    nxc_font_t *face = 0;
    nxc_font_info_t info;
    st = validate_bytes(data, size, &face, &info, reason, sizeof(reason));
    if (st != NXC_OK) {
        nx_free(data);
        set_reason(message, cap, "Cannot install: ", reason);
        reg_log("install rejected: ", reason, 0);
        return st;
    }
    if (find_duplicate(&info)) {
        nxc_font_destroy(face);
        nx_free(data);
        set_reason(message, cap, "Already installed: ", info.full_name[0] ? info.full_name : info.family);
        reg_log("install rejected: duplicate", 0, 0);
        return NXC_ERR_STATE;
    }
    for (uint32_t i = 0; i < g_reg.count; i++) {
        if (g_reg.slots[i].e.source == NXFONT_SOURCE_USER && s_eq_ci(g_reg.slots[i].file, file)) {
            nxc_font_destroy(face);
            nx_free(data);
            set_reason(message, cap, "A different font with this file name is installed", 0);
            return NXC_ERR_STATE;
        }
    }

    /* Copy into the user directory unless the file already lives there. */
    char dest[NXFONT_PATH_MAX];
    s_copy(dest, sizeof(dest), g_reg.dir);
    s_cat(dest, sizeof(dest), "/");
    s_cat(dest, sizeof(dest), file);
    int persisted = 1;
    if (!s_eq(dest, path) && write_file(dest, data, size) != NXC_OK) persisted = 0;
    slot_t *s = add_user(data, size, face, &info, file, persisted ? dest : path, 1, persisted);
    if (!s) {
        nxc_font_destroy(face);
        nx_free(data);
        set_reason(message, cap, "Too many fonts installed", 0);
        return NXC_ERR_OVERFLOW;
    }
    if (persisted && write_index() != NXC_OK) {
        persisted = 0;
        s->e.persisted = 0;
    }
    if (id_out) *id_out = s->e.id;
    if (persisted) {
        set_reason(message, cap, "Installed ", s->e.family);
        s_cat(message, cap, " ");
        s_cat(message, cap, s->e.style);
    } else {
        set_reason(message, cap, "Installed for this session only: could not save to ", g_reg.dir);
    }
    reg_log(persisted ? "installed: " : "installed (session only): ", s->e.family, 0);
    return NXC_OK;
}

nxc_status_t nxfont_set_enabled(uint32_t id, int enabled) {
    slot_t *s = slot_by_id(id);
    if (!s) return NXC_ERR_NOT_FOUND;
    if (s->e.locked) {
        reg_log("refused to change a system font: ", s->e.family, 0);
        return NXC_ERR_UNSUPPORTED;
    }
    uint8_t v = (uint8_t)(enabled != 0);
    if (s->e.enabled == v) return NXC_OK;
    s->e.enabled = v;
    reg_log(v ? "enabled: " : "disabled: ", s->e.family, 0);
    if (s->e.persisted && write_index() != NXC_OK) {
        reg_log("state change not saved (index write failed)", 0, 0);
    }
    return NXC_OK;
}

/* ============ Queries ============ */

uint32_t nxfont_count(void) { return g_reg.count; }

const nxfont_entry_t *nxfont_at(uint32_t index) {
    return index < g_reg.count ? &g_reg.slots[index].e : 0;
}

const nxfont_entry_t *nxfont_get(uint32_t id) {
    slot_t *s = slot_by_id(id);
    return s ? &s->e : 0;
}

const nxc_font_t *nxfont_face(uint32_t id) {
    slot_t *s = slot_by_id(id);
    return s ? s->face : 0;
}

uint32_t nxfont_family_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_reg.count; i++) {
        int seen = 0;
        for (uint32_t j = 0; j < i && !seen; j++) seen = s_eq_ci(g_reg.slots[j].e.family, g_reg.slots[i].e.family);
        n += !seen;
    }
    return n;
}

const char *nxfont_family_at(uint32_t index) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_reg.count; i++) {
        int seen = 0;
        for (uint32_t j = 0; j < i && !seen; j++) seen = s_eq_ci(g_reg.slots[j].e.family, g_reg.slots[i].e.family);
        if (seen) continue;
        if (n == index) return g_reg.slots[i].e.family;
        n++;
    }
    return 0;
}

static uint32_t style_key(const nxfont_entry_t *e) {
    return (uint32_t)e->info.weight * 2u + (e->info.italic ? 1u : 0u);
}

uint32_t nxfont_family_styles(const char *family, uint32_t *ids, uint32_t cap) {
    if (!family || !ids || cap == 0) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_reg.count && n < cap; i++) {
        if (!s_eq_ci(g_reg.slots[i].e.family, family)) continue;
        /* Insertion sort by (weight, italic). */
        uint32_t k = n++;
        uint32_t key = style_key(&g_reg.slots[i].e);
        while (k > 0) {
            const nxfont_entry_t *prev = nxfont_get(ids[k - 1]);
            if (!prev || style_key(prev) <= key) break;
            ids[k] = ids[k - 1];
            k--;
        }
        ids[k] = g_reg.slots[i].e.id;
    }
    return n;
}

/* CSS weight preference: lower rank = better. */
static uint32_t weight_rank(uint32_t want, uint32_t have) {
    if (have == want) return 0;
    if (want == 400 && have == 500) return 1;
    if (want == 500 && have == 400) return 1;
    if (want <= 500) {
        /* lighter first (closest), then heavier (closest) */
        if (have < want) return 10u + (want - have);
        return 2000u + (have - want);
    }
    /* heavier first, then lighter */
    if (have > want) return 10u + (have - want);
    return 2000u + (want - have);
}

const nxc_font_t *nxfont_lookup(const char *family, uint32_t weight, int italic) {
    if (!family) return 0;
    if (weight < 1) weight = 1;
    if (weight > 1000) weight = 1000;
    const slot_t *best = 0;
    uint32_t best_rank = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < g_reg.count; i++) {
        const slot_t *s = &g_reg.slots[i];
        if (!s->e.enabled || !s->face || !s_eq_ci(s->e.family, family)) continue;
        uint32_t rank = weight_rank(weight, s->e.info.weight);
        if ((s->e.info.italic != 0) != (italic != 0)) rank += 100000u;   /* slant mismatch last */
        if (rank < best_rank) { best_rank = rank; best = s; }
    }
    return best ? best->face : 0;
}

uint32_t nxfont_char_count(uint32_t id) {
    slot_t *s = slot_by_id(id);
    if (!s || !s->face) return 0;
    if (s->char_count == 0) {
        uint32_t n = 0;
        for (uint32_t cp = nxc_font_next_char(s->face, 0); cp != NXC_NO_CHAR && n < 0x110000u;
             cp = nxc_font_next_char(s->face, cp + 1)) {
            n++;
        }
        s->char_count = n ? n : 0xFFFFFFFFu;
    }
    return s->char_count == 0xFFFFFFFFu ? 0 : s->char_count;
}
