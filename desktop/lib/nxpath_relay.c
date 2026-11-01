/*
 * NeolyxOS - Path relay (see include/nxpath_relay.h)
 *
 * Kernel interfaces used (kernel/include/core/syscall_table.def):
 *   SYS_READDIR (30): readdir(path, vfs_dirent_t *entries, count, uint32_t *nread)
 *   SYS_STAT    (25): stat(path, stat_t *st)
 * Both structures are mirrored below with static size checks; they MUST
 * match kernel/include/fs/vfs.h and kernel/include/core/syscall.h.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxpath_relay.h"
#include <stddef.h>
#include "nxsyscall.h"

#define RELAY_SYS_STAT     25
#define RELAY_SYS_READDIR  30

/* Kernel vfs_dirent_t: name[256], vfs_type_t (int), inode, size. */
typedef struct {
    char     name[256];
    int32_t  type;
    uint64_t inode;
    uint64_t size;
} relay_kdirent_t;
_Static_assert(sizeof(relay_kdirent_t) == 280, "vfs_dirent_t layout changed");

/* Kernel stat_t. */
typedef struct {
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
} relay_kstat_t;
_Static_assert(sizeof(relay_kstat_t) == 56, "stat_t layout changed");

/* Kernel VFS error codes (kernel/include/fs/vfs.h) and errno values. */
#define K_VFS_NOT_FOUND  (-1)
#define K_VFS_PERM       (-2)
#define K_VFS_NOT_DIR    (-6)
#define K_EFAULT         (-14)
#define K_ENOSYS         (-38)

/* ============ Small helpers (freestanding) ============ */

static int r_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void r_strcpy(char *d, const char *s, int cap) {
    int i = 0;
    if (cap <= 0) return;
    for (; i < cap - 1 && s[i]; i++) d[i] = s[i];
    d[i] = '\0';
}

static int r_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char r_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* Case-insensitive compare for sorting. */
static int r_cmp_ci(const char *a, const char *b) {
    while (*a && r_lower(*a) == r_lower(*b)) { a++; b++; }
    return (int)(unsigned char)r_lower(*a) - (int)(unsigned char)r_lower(*b);
}

static void r_zero(void *p, unsigned n) {
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (unsigned i = 0; i < n; i++) b[i] = 0;
}

static void relay_log(const char *msg, const char *path, int err) {
    char line[NXRELAY_PATH_MAX + 96];
    char num[16];
    int n = 0;
    const char *parts[3] = { "[RELAY] ", msg, path ? " path=" : "" };
    for (int p = 0; p < 3; p++)
        for (const char *s = parts[p]; *s && n < (int)sizeof(line) - 24; s++) line[n++] = *s;
    if (path)
        for (const char *s = path; *s && n < (int)sizeof(line) - 24; s++) line[n++] = *s;
    if (err) {
        const char *e = " err=";
        for (const char *s = e; *s; s++) line[n++] = *s;
        int v = err < 0 ? -err : err, k = 0;
        if (err < 0) line[n++] = '-';
        do { num[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 15);
        while (k) line[n++] = num[--k];
    }
    line[n++] = '\n';
    line[n] = '\0';
    nx_debug_print(line);
}

/* ============ Path rules ============ */

static int name_char_ok(char c) {
    /* No control characters, no separators inside a component. */
    return (unsigned char)c >= 0x20 && c != 0x7F && c != '/' && c != '\\';
}

int nxrelay_normalize(const char *in, char *out, int cap) {
    if (!in || !out || cap < 2) return NXRELAY_ERR_INVALID;
    out[0] = '\0';
    if (in[0] != '/') return NXRELAY_ERR_INVALID;
    int n = 0;
    out[n++] = '/';
    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/') {
            if (!name_char_ok(*p)) return NXRELAY_ERR_INVALID;
            p++;
        }
        int len = (int)(p - s);
        if (len == 1 && s[0] == '.') continue;                  /* "." */
        if (len == 2 && s[0] == '.' && s[1] == '.') return NXRELAY_ERR_INVALID;   /* ".." */
        if (len > NXRELAY_NAME_MAX) return NXRELAY_ERR_INVALID;
        if (n > 1) {
            if (n >= cap - 1) return NXRELAY_ERR_INVALID;
            out[n++] = '/';
        }
        if (n + len >= cap || n + len >= NXRELAY_PATH_MAX) return NXRELAY_ERR_INVALID;
        for (int i = 0; i < len; i++) out[n++] = s[i];
    }
    out[n] = '\0';
    return NXRELAY_OK;
}

int nxrelay_join(const char *dir, const char *name, char *out, int cap) {
    if (!dir || !name || !out || cap < 2 || !name[0]) return NXRELAY_ERR_INVALID;
    for (const char *s = name; *s; s++) if (!name_char_ok(*s)) return NXRELAY_ERR_INVALID;
    if (r_streq(name, ".") || r_streq(name, "..")) return NXRELAY_ERR_INVALID;
    char tmp[NXRELAY_PATH_MAX * 2];
    int dl = r_strlen(dir), nl = r_strlen(name);
    if (dl + nl + 2 > (int)sizeof(tmp)) return NXRELAY_ERR_INVALID;
    r_strcpy(tmp, dir, (int)sizeof(tmp));
    tmp[dl] = '/';
    r_strcpy(tmp + dl + 1, name, (int)sizeof(tmp) - dl - 1);
    return nxrelay_normalize(tmp, out, cap);
}

int nxrelay_parent(const char *path, char *out, int cap) {
    char canon[NXRELAY_PATH_MAX];
    if (nxrelay_normalize(path, canon, (int)sizeof(canon)) != NXRELAY_OK) return NXRELAY_ERR_INVALID;
    int n = r_strlen(canon);
    while (n > 1 && canon[n - 1] != '/') n--;
    if (n > 1) n--;                 /* drop the separator, keep "/" for top level */
    canon[n] = '\0';
    if (n + 1 > cap) return NXRELAY_ERR_INVALID;
    r_strcpy(out, canon, cap);
    return NXRELAY_OK;
}

const char *nxrelay_basename(const char *path) {
    if (!path || !path[0]) return "";
    if (path[0] == '/' && path[1] == '\0') return "/";
    const char *b = path;
    for (const char *s = path; *s; s++) if (*s == '/' && s[1]) b = s + 1;
    return b;
}

/* ============ Errors ============ */

static int map_kernel_err(int64_t r) {
    switch (r) {
        case K_VFS_NOT_FOUND: return NXRELAY_ERR_NOT_FOUND;
        case K_VFS_PERM:      return NXRELAY_ERR_DENIED;
        case K_VFS_NOT_DIR:   return NXRELAY_ERR_NOT_DIR;
        case K_ENOSYS:        return NXRELAY_ERR_NO_KERNEL;
        case K_EFAULT:        return NXRELAY_ERR_INVALID;
        default:              return NXRELAY_ERR_IO;
    }
}

const char *nxrelay_strerror(int err) {
    switch (err) {
        case NXRELAY_OK:            return "OK";
        case NXRELAY_ERR_INVALID:   return "This location name is not valid.";
        case NXRELAY_ERR_NOT_FOUND: return "This folder does not exist.";
        case NXRELAY_ERR_NOT_DIR:   return "This item is not a folder.";
        case NXRELAY_ERR_DENIED:    return "You do not have permission to open this folder.";
        case NXRELAY_ERR_NO_KERNEL: return "The system cannot list files yet.";
        default:                    return "The folder could not be read.";
    }
}

/* ============ User and places ============ */

static char g_user[32];

int nxrelay_set_user(const char *name) {
    if (!name) return NXRELAY_ERR_INVALID;
    int n = r_strlen(name);
    if (n < 1 || n > 31 || name[0] == '.') return NXRELAY_ERR_INVALID;
    for (int i = 0; i < n; i++) {
        char c = name[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '.' || c == '_' || c == '-';
        if (!ok) return NXRELAY_ERR_INVALID;
    }
    r_strcpy(g_user, name, (int)sizeof(g_user));
    relay_log("user set", NULL, 0);
    nxrelay_invalidate(NULL);
    return NXRELAY_OK;
}

static const struct { const char *name; const char *sub; } k_places[NXRELAY_LOC_COUNT] = {
    { "Home",         "" },
    { "Desktop",      "Desktop" },
    { "Documents",    "Documents" },
    { "Downloads",    "Downloads" },
    { "Pictures",     "Pictures" },
    { "Music",        "Music" },
    { "Applications", "/Applications" },
    { "System",       "/System" },
    { "Volumes",      "/Volumes" },
    { "Computer",     "/" },
};

const char *nxrelay_location_name(nxrelay_loc_t loc) {
    if ((int)loc < 0 || loc >= NXRELAY_LOC_COUNT) return "";
    return k_places[loc].name;
}

int nxrelay_location(nxrelay_loc_t loc, char *out, int cap) {
    if (!out || cap < 2 || (int)loc < 0 || loc >= NXRELAY_LOC_COUNT) return NXRELAY_ERR_INVALID;
    const char *sub = k_places[loc].sub;
    if (sub[0] == '/') return nxrelay_normalize(sub, out, cap);
    /* Home-relative: /Users/<name>, or /Users when nobody is signed in. */
    char home[64] = "/Users";
    if (g_user[0]) {
        r_strcpy(home + 6, "/", (int)sizeof(home) - 6);
        r_strcpy(home + 7, g_user, (int)sizeof(home) - 7);
    }
    if (!sub[0]) return nxrelay_normalize(home, out, cap);
    return nxrelay_join(home, sub, out, cap);
}

/* ============ Kernel access ============ */

int nxrelay_stat(const char *path, nxrelay_kind_t *kind, uint64_t *size) {
    char canon[NXRELAY_PATH_MAX];
    int r = nxrelay_normalize(path, canon, (int)sizeof(canon));
    if (r != NXRELAY_OK) return r;
    relay_kstat_t st;
    r_zero(&st, sizeof(st));
    int64_t k = syscall2(RELAY_SYS_STAT, (int64_t)(uintptr_t)canon, (int64_t)(uintptr_t)&st);
    /* SYS_STAT answers -EFAULT for a bad pointer and -ENOENT (-2) for every
     * failed lookup. -2 is also the VFS "permission" code that
     * map_kernel_err expects from other calls, so map it here: a missing
     * item must read as "not found", not "denied". */
    if (k == -2) return NXRELAY_ERR_NOT_FOUND;
    if (k != 0) return map_kernel_err(k);
    if (kind) *kind = (st.st_mode & 0170000u) == 0040000u ? NXRELAY_KIND_FOLDER : NXRELAY_KIND_FILE;
    if (size) *size = st.st_size;
    return NXRELAY_OK;
}

/* ============ Home folders ============ */

int nxrelay_ensure_home(void) {
    if (!g_user[0]) return NXRELAY_ERR_INVALID;
    static const nxrelay_loc_t subs[] = {
        NXRELAY_LOC_HOME, NXRELAY_LOC_DESKTOP, NXRELAY_LOC_DOCUMENTS, NXRELAY_LOC_DOWNLOADS,
        NXRELAY_LOC_PICTURES, NXRELAY_LOC_MUSIC,
    };
    int rc = NXRELAY_OK;
    nxrelay_kind_t kind;
    if (nxrelay_stat("/Users", &kind, NULL) != NXRELAY_OK) (void)nx_mkdir("/Users");
    for (unsigned i = 0; i < sizeof(subs) / sizeof(subs[0]); i++) {
        char p[NXRELAY_PATH_MAX];
        if (nxrelay_location(subs[i], p, (int)sizeof(p)) != NXRELAY_OK) { rc = NXRELAY_ERR_INVALID; continue; }
        if (nxrelay_stat(p, &kind, NULL) == NXRELAY_OK) {
            if (kind != NXRELAY_KIND_FOLDER) { relay_log("home: not a folder", p, 0); rc = NXRELAY_ERR_NOT_DIR; }
            continue;
        }
        int r = nx_mkdir(p);
        if (r != 0 && !(nxrelay_stat(p, &kind, NULL) == NXRELAY_OK && kind == NXRELAY_KIND_FOLDER)) {
            relay_log("home: could not create", p, r);
            rc = NXRELAY_ERR_IO;
        } else {
            relay_log("home: created", p, 0);
        }
    }
    nxrelay_invalidate(NULL);
    return rc;
}

/* ============ Listing cache ============ */

#define RELAY_SLOTS 3
#define RELAY_TTL_MS 5000u    /* a listing older than this is reloaded on the next ask */

typedef struct {
    int               used;
    uint32_t          flags;      /* NXRELAY_SHOW_HIDDEN as listed */
    uint64_t          last_use;
    nxrelay_listing_t view;
    nxrelay_entry_t   entries[NXRELAY_MAX_ENTRIES];
} relay_slot_t;

static relay_slot_t   g_slots[RELAY_SLOTS];
static relay_kdirent_t g_kbuf[NXRELAY_MAX_ENTRIES + 1];
static uint32_t       g_generation = 1;
static uint64_t       g_use_clock;
static nxrelay_listing_t g_bad;   /* returned for invalid paths */

uint32_t nxrelay_generation(void) { return g_generation; }

void nxrelay_invalidate(const char *path) {
    char canon[NXRELAY_PATH_MAX];
    if (path && nxrelay_normalize(path, canon, (int)sizeof(canon)) != NXRELAY_OK) return;
    for (int i = 0; i < RELAY_SLOTS; i++) {
        if (!g_slots[i].used) continue;
        if (!path || r_streq(g_slots[i].view.path, canon)) g_slots[i].used = 0;
    }
}

static int entry_before(const nxrelay_entry_t *a, const nxrelay_entry_t *b) {
    int fa = a->kind == NXRELAY_KIND_FOLDER, fb = b->kind == NXRELAY_KIND_FOLDER;
    if (fa != fb) return fa > fb;
    int c = r_cmp_ci(a->name, b->name);
    return c < 0;
}

static void sort_entries(nxrelay_entry_t *e, uint32_t n) {
    /* Insertion sort: n <= 256 and listings are usually nearly sorted. */
    for (uint32_t i = 1; i < n; i++) {
        nxrelay_entry_t t = e[i];
        uint32_t j = i;
        while (j > 0 && entry_before(&t, &e[j - 1])) { e[j] = e[j - 1]; j--; }
        e[j] = t;
    }
}

static void add_entry(relay_slot_t *s, const char *name, int kind, uint64_t size, int size_known, uint32_t flags) {
    if (!name[0] || r_streq(name, ".") || r_streq(name, "..")) return;
    for (const char *p = name; *p; p++) if (!name_char_ok(*p)) return;   /* unusable name */
    int hidden = name[0] == '.';
    if (hidden && !(flags & NXRELAY_SHOW_HIDDEN)) return;
    if (s->view.count >= NXRELAY_MAX_ENTRIES) { s->view.truncated = 1; return; }
    nxrelay_entry_t *e = &s->entries[s->view.count++];
    r_strcpy(e->name, name, (int)sizeof(e->name));
    e->kind = (uint8_t)kind;
    e->hidden = (uint8_t)hidden;
    e->size = size;
    e->size_known = (uint8_t)size_known;
}

/* "/" with nothing mounted at the root: the standard top-level folders
 * that really exist (each checked with SYS_STAT). */
static int synthesize_root(relay_slot_t *s, uint32_t flags) {
    static const char *const tops[] = {
        "Applications", "System", "Users", "Library", "Volumes", "Boot", "Developer", "Games", "ramfs",
    };
    for (unsigned i = 0; i < sizeof(tops) / sizeof(tops[0]); i++) {
        char p[64] = "/";
        r_strcpy(p + 1, tops[i], (int)sizeof(p) - 1);
        nxrelay_kind_t kind;
        if (nxrelay_stat(p, &kind, NULL) == NXRELAY_OK && kind == NXRELAY_KIND_FOLDER)
            add_entry(s, tops[i], NXRELAY_KIND_FOLDER, 0, 0, flags);
    }
    s->view.synthesized = 1;
    return s->view.count > 0 ? NXRELAY_OK : NXRELAY_ERR_NOT_FOUND;
}

static void load_slot(relay_slot_t *s, const char *canon, uint32_t flags) {
    r_strcpy(s->view.path, canon, (int)sizeof(s->view.path));
    s->view.count = 0;
    s->view.truncated = 0;
    s->view.synthesized = 0;
    s->view.entries = s->entries;
    s->flags = flags & NXRELAY_SHOW_HIDDEN;

    r_zero(g_kbuf, sizeof(g_kbuf));
    uint32_t nread = 0;
    int64_t k = syscall4(RELAY_SYS_READDIR, (int64_t)(uintptr_t)canon, (int64_t)(uintptr_t)g_kbuf,
                         (int64_t)(NXRELAY_MAX_ENTRIES + 1), (int64_t)(uintptr_t)&nread);
    if (k == 0) {
        if (nread > NXRELAY_MAX_ENTRIES + 1) nread = NXRELAY_MAX_ENTRIES + 1;   /* never trust the count */
        for (uint32_t i = 0; i < nread; i++) {
            relay_kdirent_t *d = &g_kbuf[i];
            d->name[sizeof(d->name) - 1] = '\0';
            int kind = d->type == 2 ? NXRELAY_KIND_FOLDER : d->type == 3 ? NXRELAY_KIND_LINK :
                       d->type == 4 ? NXRELAY_KIND_DEVICE : NXRELAY_KIND_FILE;
            /* The kernel does not fill sizes for every file system yet. */
            add_entry(s, d->name, kind, d->size, kind == NXRELAY_KIND_FILE && d->size > 0, flags);
        }
        s->view.status = NXRELAY_OK;
    } else if (canon[0] == '/' && canon[1] == '\0') {
        s->view.status = synthesize_root(s, flags);
        static int logged_synth;
        if (s->view.status == NXRELAY_OK && !logged_synth) {
            logged_synth = 1;
            relay_log("root not listable; showing known places", canon, (int)k);
        }
    } else {
        s->view.status = map_kernel_err(k);
    }
    /* Log a failure once per (path, error), not on every periodic reload. */
    static char last_fail[NXRELAY_PATH_MAX];
    static int last_err;
    if (s->view.status != NXRELAY_OK) {
        if (last_err != s->view.status || !r_streq(last_fail, canon)) {
            relay_log("list failed", canon, (int)k);
            r_strcpy(last_fail, canon, (int)sizeof(last_fail));
            last_err = s->view.status;
        }
    } else if (r_streq(last_fail, canon)) {
        last_fail[0] = '\0';
        last_err = 0;
    }
    sort_entries(s->entries, s->view.count);
    s->view.loaded_ms = nx_gettime();
    s->view.generation = ++g_generation;
    s->used = 1;
}

const nxrelay_listing_t *nxrelay_list(const char *path, uint32_t flags) {
    char canon[NXRELAY_PATH_MAX];
    int r = path ? nxrelay_normalize(path, canon, (int)sizeof(canon)) : NXRELAY_ERR_INVALID;
    if (r != NXRELAY_OK) {
        r_zero(&g_bad, sizeof(g_bad));
        if (path) r_strcpy(g_bad.path, path, (int)sizeof(g_bad.path));
        g_bad.status = NXRELAY_ERR_INVALID;
        g_bad.entries = g_slots[0].entries;
        relay_log("rejected path", NULL, r);
        return &g_bad;
    }
    uint64_t now = nx_gettime();
    uint32_t want = flags & NXRELAY_SHOW_HIDDEN;
    relay_slot_t *hit = 0, *victim = &g_slots[0];
    for (int i = 0; i < RELAY_SLOTS; i++) {
        relay_slot_t *s = &g_slots[i];
        if (s->used && s->flags == want && r_streq(s->view.path, canon)) { hit = s; break; }
        if (!s->used) victim = s;
        else if (victim->used && s->last_use < victim->last_use) victim = s;
    }
    relay_slot_t *s = hit ? hit : victim;
    s->last_use = ++g_use_clock;
    if (!hit || (flags & NXRELAY_REFRESH) || now - s->view.loaded_ms > RELAY_TTL_MS)
        load_slot(s, canon, flags);
    return &s->view;
}

/* ============ Self-test ============ */

int nxrelay_selftest(void) {
    char o[NXRELAY_PATH_MAX];
    int bad = 0;
    bad += nxrelay_normalize("/a//b/./c/", o, sizeof(o)) != 0 || !r_streq(o, "/a/b/c");
    bad += nxrelay_normalize("/", o, sizeof(o)) != 0 || !r_streq(o, "/");
    bad += nxrelay_normalize("relative", o, sizeof(o)) != NXRELAY_ERR_INVALID;
    bad += nxrelay_normalize("/a/../etc", o, sizeof(o)) != NXRELAY_ERR_INVALID;
    bad += nxrelay_normalize("/a/\x01", o, sizeof(o)) != NXRELAY_ERR_INVALID;
    bad += nxrelay_parent("/Users/me", o, sizeof(o)) != 0 || !r_streq(o, "/Users");
    bad += nxrelay_parent("/Users", o, sizeof(o)) != 0 || !r_streq(o, "/");
    bad += nxrelay_parent("/", o, sizeof(o)) != 0 || !r_streq(o, "/");
    bad += nxrelay_join("/Users", "me", o, sizeof(o)) != 0 || !r_streq(o, "/Users/me");
    bad += nxrelay_join("/", "System", o, sizeof(o)) != 0 || !r_streq(o, "/System");
    bad += nxrelay_join("/a", "..", o, sizeof(o)) != NXRELAY_ERR_INVALID;
    bad += nxrelay_join("/a", "b/c", o, sizeof(o)) != NXRELAY_ERR_INVALID;
    bad += !r_streq(nxrelay_basename("/Users/me"), "me");
    bad += nxrelay_set_user("../x") != NXRELAY_ERR_INVALID;
    if (bad) relay_log("selftest failures", NULL, bad);
    return bad;
}
