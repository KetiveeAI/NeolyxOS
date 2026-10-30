/*
 * NeolyxOS - Font registry tests (lib/nxfont_registry.c)
 *
 * The registry talks to the kernel through nxsyscall.h; built with
 * NX_HOST_PREVIEW those wrappers call nx_host_syscall(), implemented here
 * over a private host directory that stands in for the NeolyxOS VFS root.
 * Covers system registration, family/weight lookup, install validation
 * (hostile files and paths), index persistence and enable/disable.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#define _POSIX_C_SOURCE 200809L
#include "nxc_test.h"
#include "../../../include/nxsyscall.h"
#include "../../../include/nxfont_registry.h"
#include <sys/stat.h>
#include <unistd.h>

/* ============ Emulated VFS ============ */

static char g_root[512];
static FILE *g_fds[16];
static int g_quiet = 1;

static void host_path(char *out, size_t cap, const char *vfs) {
    snprintf(out, cap, "%s%s", g_root, vfs);
}

int64_t nx_host_syscall(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    switch (num) {
        case SYS_OPEN: {
            const char *p = (const char *)(uintptr_t)a1;
            if (!p) return -14;
            char hp[1024];
            host_path(hp, sizeof(hp), p);
            int fl = (int)a2;
            const char *mode = (fl & O_WRONLY) ? ((fl & O_TRUNC) ? "wb" : "ab") : "rb";
            FILE *f = fopen(hp, mode);
            if (!f) return -2;
            for (int i = 0; i < 16; i++) if (!g_fds[i]) { g_fds[i] = f; return i; }
            fclose(f);
            return -24;
        }
        case SYS_CLOSE:
            if (a1 < 0 || a1 >= 16 || !g_fds[a1]) return -9;
            fclose(g_fds[a1]); g_fds[a1] = NULL;
            return 0;
        case SYS_READ:
            if (a1 < 0 || a1 >= 16 || !g_fds[a1]) return -9;
            return (int64_t)fread((void *)(uintptr_t)a2, 1, (size_t)a3, g_fds[a1]);
        case SYS_WRITE:
            if (a1 < 0 || a1 >= 16 || !g_fds[a1]) return -9;
            return (int64_t)fwrite((const void *)(uintptr_t)a2, 1, (size_t)a3, g_fds[a1]);
        case SYS_SEEK:
            if (a1 < 0 || a1 >= 16 || !g_fds[a1]) return -9;
            if (fseek(g_fds[a1], (long)a2, (int)a3) != 0) return -22;
            return (int64_t)ftell(g_fds[a1]);
        case 127:
            if (!g_quiet && a1) fputs((const char *)(uintptr_t)a1, stderr);
            return 0;
        default:
            return -38;
    }
}

void *nx_malloc(uint64_t size) {
    void *p = NULL;
    if (posix_memalign(&p, 16, size ? size : 16) != 0) return NULL;
    return p;
}
void nx_mfree(void *ptr) { free(ptr); }

/* ============ Helpers ============ */

static uint8_t *read_host(const char *path, uint32_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) exit(2);
    fclose(f);
    *size = (uint32_t)n;
    return d;
}

static void write_vfs(const char *vfs, const void *data, size_t n) {
    char hp[1024];
    host_path(hp, sizeof(hp), vfs);
    FILE *f = fopen(hp, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", hp); exit(2); }
    fwrite(data, 1, n, f);
    fclose(f);
}

static int vfs_exists(const char *vfs) {
    char hp[1024];
    host_path(hp, sizeof(hp), vfs);
    struct stat st;
    return stat(hp, &st) == 0;
}

static char *read_vfs_text(const char *vfs) {
    static char buf[8192];
    char hp[1024];
    host_path(hp, sizeof(hp), vfs);
    FILE *f = fopen(hp, "rb");
    if (!f) { buf[0] = 0; return buf; }
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static const char *font_dir(void) {
    const char *d = getenv("NXC_FONT_DIR");
    return (d && *d) ? d : "../../../../resources/fonts/";
}

typedef struct { uint8_t *data; uint32_t size; nxc_font_t *font; } face_t;

static face_t load(const char *rel) {
    char p[1024];
    snprintf(p, sizeof(p), "%s%s", font_dir(), rel);
    face_t fc;
    fc.data = read_host(p, &fc.size);
    fc.font = NULL;
    NXC_CHECK_OK(nxc_font_load(&fc.font, fc.data, fc.size));
    return fc;
}

/* ============ Tests ============ */

int main(void) {
    nxc_test_init(256, 256);
    snprintf(g_root, sizeof(g_root), "%s/vfs_XXXXXX", nxc_test_out_dir());
    mkdir(nxc_test_out_dir(), 0755);
    if (!mkdtemp(g_root)) { perror("mkdtemp"); return 2; }
    char hp[1024];
    host_path(hp, sizeof(hp), "/Library"); mkdir(hp, 0755);
    host_path(hp, sizeof(hp), "/Library/Fonts"); mkdir(hp, 0755);
    host_path(hp, sizeof(hp), "/Users"); mkdir(hp, 0755);

    face_t text = load("NXSans/NXSans-Text.ttf");
    face_t ui = load("NXSans/NXSans-UI.ttf");
    face_t semi = load("NXSans/NXSans-Display.ttf");
    face_t ital = load("NXSans/NXSans-Italic.ttf");
    face_t mono = load("NXSansMono/NXSans-Mono.ttf");

    /* ---- System registration and lookup ---- */
    NXC_CHECK_OK(nxfont_registry_init());
    uint32_t id_text = 0, id_mono = 0;
    NXC_CHECK_OK(nxfont_registry_add_system(text.font, "NX Sans", "Regular", "text", &id_text));
    NXC_CHECK_OK(nxfont_registry_add_system(ui.font, "NX Sans", "Medium", "ui", NULL));
    NXC_CHECK_OK(nxfont_registry_add_system(semi.font, "NX Sans", "Semibold", "display", NULL));
    NXC_CHECK_OK(nxfont_registry_add_system(ital.font, "NX Sans", "Italic", "emphasis", NULL));
    NXC_CHECK_OK(nxfont_registry_add_system(mono.font, "NX Sans Mono", "Regular", "mono", &id_mono));
    NXC_CHECK(nxfont_registry_add_system(NULL, "X", "Y", NULL, NULL) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxfont_registry_add_system(text.font, "", "Y", NULL, NULL) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxfont_count() == 5 && nxfont_family_count() == 2);
    NXC_CHECK(strcmp(nxfont_family_at(0), "NX Sans") == 0 && strcmp(nxfont_family_at(1), "NX Sans Mono") == 0);
    NXC_CHECK(nxfont_family_at(2) == NULL);
    uint32_t ids[8];
    uint32_t n = nxfont_family_styles("nx sans", ids, 8);
    NXC_CHECK(n == 4);
    if (n == 4) {
        NXC_CHECK(strcmp(nxfont_get(ids[0])->style, "Regular") == 0);
        NXC_CHECK(strcmp(nxfont_get(ids[1])->style, "Italic") == 0);
        NXC_CHECK(strcmp(nxfont_get(ids[2])->style, "Medium") == 0);
        NXC_CHECK(strcmp(nxfont_get(ids[3])->style, "Semibold") == 0);
    }
    NXC_CHECK(nxfont_lookup("NX Sans", 400, 0) == text.font);
    NXC_CHECK(nxfont_lookup("nx sans", 500, 0) == ui.font);
    NXC_CHECK(nxfont_lookup("NX Sans", 300, 0) == text.font);    /* nothing lighter: heavier */
    NXC_CHECK(nxfont_lookup("NX Sans", 700, 0) == semi.font);    /* heavier first: none, then 600 */
    NXC_CHECK(nxfont_lookup("NX Sans", 450, 0) == text.font);
    NXC_CHECK(nxfont_lookup("NX Sans", 400, 1) == ital.font);
    NXC_CHECK(nxfont_lookup("NX Sans Mono", 700, 1) == mono.font);
    NXC_CHECK(nxfont_lookup("Comic", 400, 0) == NULL);
    NXC_CHECK(nxfont_set_enabled(id_text, 0) == NXC_ERR_UNSUPPORTED);
    NXC_CHECK(nxfont_get(id_text)->enabled == 1 && nxfont_get(id_text)->locked == 1);
    NXC_CHECK(nxfont_char_count(id_text) > 1500);
    NXC_CHECK(nxfont_face(id_mono) == mono.font && nxfont_face(9999) == NULL);

    /* ---- No index yet ---- */
    uint32_t loaded = 99;
    NXC_CHECK(nxfont_registry_load_user(NULL, &loaded) == NXC_ERR_NOT_FOUND && loaded == 0);
    NXC_CHECK(nxfont_registry_load_user("relative/dir", NULL) == NXC_ERR_INVALID_ARG);

    /* ---- Hostile installs ---- */
    char path[1024];
    snprintf(path, sizeof(path), "%sNXSans/NXSans-Bold.ttf", font_dir());
    uint32_t bold_size = 0;
    uint8_t *bold = read_host(path, &bold_size);
    char msg[NXFONT_REASON_MAX];
    uint32_t id = 0;

    uint8_t junk[4096];
    uint32_t seed = 7;
    for (size_t i = 0; i < sizeof(junk); i++) { seed = seed * 1103515245u + 12345u; junk[i] = (uint8_t)(seed >> 16); }
    write_vfs("/Users/junk.ttf", junk, sizeof(junk));
    NXC_CHECK(nxfont_install("/Users/junk.ttf", &id, msg, sizeof(msg)) == NXC_ERR_BAD_FONT && id == 0);
    fprintf(stderr, "junk: %s\n", msg);
    NXC_CHECK(strstr(msg, "invalid font file") != NULL);

    write_vfs("/Users/trunc.ttf", bold, 20000);
    NXC_CHECK(nxfont_install("/Users/trunc.ttf", &id, msg, sizeof(msg)) != NXC_OK);
    fprintf(stderr, "truncated: %s\n", msg);

    write_vfs("/Users/tiny.ttf", bold, 100);
    NXC_CHECK(nxfont_install("/Users/tiny.ttf", &id, msg, sizeof(msg)) == NXC_ERR_BAD_FONT);
    NXC_CHECK(strstr(msg, "too small") != NULL);

    /* Header intact, table offsets pointing past the end. */
    uint8_t *bad = malloc(bold_size);
    memcpy(bad, bold, bold_size);
    bad[12 + 8] = 0x7F;                         /* first table offset -> huge */
    write_vfs("/Users/badoff.ttf", bad, bold_size);
    NXC_CHECK(nxfont_install("/Users/badoff.ttf", &id, msg, sizeof(msg)) == NXC_ERR_BAD_FONT);
    /* Absurd table count. */
    memcpy(bad, bold, bold_size);
    bad[4] = 0xFF; bad[5] = 0xFF;
    write_vfs("/Users/badcount.ttf", bad, bold_size);
    NXC_CHECK(nxfont_install("/Users/badcount.ttf", &id, msg, sizeof(msg)) == NXC_ERR_BAD_FONT);
    free(bad);

    NXC_CHECK(nxfont_install("/Users/../etc/x.ttf", &id, msg, sizeof(msg)) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxfont_install("Users/x.ttf", &id, msg, sizeof(msg)) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxfont_install("/Users/x.otf", &id, msg, sizeof(msg)) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxfont_install("/Users/we ird.ttf", &id, msg, sizeof(msg)) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxfont_install("/Users/missing.ttf", &id, msg, sizeof(msg)) == NXC_ERR_NOT_FOUND);
    fprintf(stderr, "missing: %s\n", msg);
    NXC_CHECK(nxfont_install(NULL, &id, msg, sizeof(msg)) == NXC_ERR_INVALID_ARG);
    nxc_font_info_t vi;
    NXC_CHECK(nxfont_validate_file("/Users/junk.ttf", &vi, msg, sizeof(msg)) == NXC_ERR_BAD_FONT);
    NXC_CHECK(nxfont_count() == 5);

    /* Duplicate of a registered face. */
    char semi_path[1024];
    snprintf(semi_path, sizeof(semi_path), "%sNXSans/NXSans-Display.ttf", font_dir());
    uint32_t semi_size = 0;
    uint8_t *semi_bytes = read_host(semi_path, &semi_size);
    write_vfs("/Users/Semi.ttf", semi_bytes, semi_size);
    NXC_CHECK(nxfont_install("/Users/Semi.ttf", &id, msg, sizeof(msg)) == NXC_ERR_STATE);
    fprintf(stderr, "duplicate: %s\n", msg);
    free(semi_bytes);

    /* ---- Valid install ---- */
    write_vfs("/Users/Heading-Bold.ttf", bold, bold_size);
    NXC_CHECK_OK(nxfont_validate_file("/Users/Heading-Bold.ttf", &vi, msg, sizeof(msg)));
    NXC_CHECK(vi.weight == 700);
    NXC_CHECK_OK(nxfont_install("/Users/Heading-Bold.ttf", &id, msg, sizeof(msg)));
    fprintf(stderr, "install: %s\n", msg);
    NXC_CHECK(id != 0 && nxfont_count() == 6);
    const nxfont_entry_t *e = nxfont_get(id);
    NXC_CHECK(e && e->source == NXFONT_SOURCE_USER && e->enabled && e->persisted && !e->locked);
    NXC_CHECK(e && strcmp(e->family, "Source Sans 3") == 0 && strcmp(e->style, "Bold") == 0);
    NXC_CHECK(vfs_exists("/Library/Fonts/Heading-Bold.ttf"));
    NXC_CHECK(strstr(read_vfs_text("/Library/Fonts/fonts.index"), "1 Heading-Bold.ttf\n") != NULL);
    NXC_CHECK(nxfont_lookup("Source Sans 3", 700, 0) == nxfont_face(id));
    NXC_CHECK(nxfont_family_count() == 3);
    /* Same file again: duplicate. */
    NXC_CHECK(nxfont_install("/Users/Heading-Bold.ttf", NULL, msg, sizeof(msg)) == NXC_ERR_STATE);

    /* ---- Disable / enable ---- */
    NXC_CHECK_OK(nxfont_set_enabled(id, 0));
    NXC_CHECK(nxfont_lookup("Source Sans 3", 700, 0) == NULL);
    NXC_CHECK(nxfont_face(id) != NULL);                 /* still previewable */
    NXC_CHECK(strstr(read_vfs_text("/Library/Fonts/fonts.index"), "0 Heading-Bold.ttf\n") != NULL);
    NXC_CHECK(nxfont_set_enabled(424242, 1) == NXC_ERR_NOT_FOUND);

    /* ---- Reload from a hostile index ---- */
    write_vfs("/Library/Fonts/junk.ttf", junk, sizeof(junk));
    const char *idx =
        "# test index\n"
        "0 Heading-Bold.ttf\r\n"
        "1 ../evil.ttf\n"
        "1 /abs/path.ttf\n"
        "garbage line\n"
        "2 Heading-Bold.ttf\n"
        "1 missing.ttf\n"
        "1 junk.ttf\n"
        "1 Heading-Bold.ttf\n"                          /* duplicate */
        "1 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.ttf\n";
    write_vfs("/Library/Fonts/fonts.index", idx, strlen(idx));
    NXC_CHECK_OK(nxfont_registry_init());
    NXC_CHECK(nxfont_count() == 0);
    NXC_CHECK_OK(nxfont_registry_add_system(text.font, "NX Sans", "Regular", "text", NULL));
    g_quiet = 0;
    NXC_CHECK_OK(nxfont_registry_load_user(NULL, &loaded));
    g_quiet = 1;
    NXC_CHECK(loaded == 1 && nxfont_count() == 2);
    e = nxfont_at(1);
    NXC_CHECK(e && e->source == NXFONT_SOURCE_USER && e->enabled == 0);
    NXC_CHECK(nxfont_at(2) == NULL);

    /* Oversized index is refused rather than truncated silently. */
    char *huge = malloc(NXFONT_INDEX_MAX + 100);
    memset(huge, '#', NXFONT_INDEX_MAX + 100);
    write_vfs("/Library/Fonts/fonts.index", huge, NXFONT_INDEX_MAX + 100);
    free(huge);
    NXC_CHECK_OK(nxfont_registry_init());
    NXC_CHECK(nxfont_registry_load_user(NULL, &loaded) == NXC_ERR_NOT_FOUND);

    NXC_CHECK_OK(nxfont_registry_init());
    free(bold);
    nxc_font_destroy(text.font); free(text.data);
    nxc_font_destroy(ui.font); free(ui.data);
    nxc_font_destroy(semi.font); free(semi.data);
    nxc_font_destroy(ital.font); free(ital.data);
    nxc_font_destroy(mono.font); free(mono.data);
    for (int i = 0; i < 16; i++) if (g_fds[i]) { NXC_CHECK(0 && "fd leaked"); fclose(g_fds[i]); }
    nxc_shutdown();
    return nxc_test_finish("registry");
}
