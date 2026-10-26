/*
 * NeolyxOS - NXCanvas host test harness implementation
 *
 * Includes a minimal PNG encoder (stored deflate blocks, CRC32, Adler-32)
 * written for this harness so the tests need no image libraries.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_test.h"
#include <sys/stat.h>

int nxc_test_failures = 0;

static void *host_alloc(uint64_t size) {
    void *p = NULL;
    if (posix_memalign(&p, 16, (size_t)size) != 0) return NULL;
    return p;
}
static void host_free(void *p) { free(p); }
static void host_log(const char *msg) { fputs(msg, stderr); }

void nxc_test_init(int32_t max_w, int32_t max_h) {
    nxc_env_t env;
    memset(&env, 0, sizeof(env));
    env.alloc = host_alloc;
    env.free = host_free;
    env.log = host_log;
    env.max_width = max_w;
    env.max_height = max_h;
    nxc_status_t st = nxc_init(&env);
    if (st != NXC_OK) {
        fprintf(stderr, "nxc_init failed: %s\n", nxc_status_str(st));
        exit(2);
    }
}

const char *nxc_test_out_dir(void) {
    const char *d = getenv("NXC_TEST_OUT");
    if (!d || !*d) d = "out";
    mkdir(d, 0755);
    return d;
}

const char *nxc_test_path(const char *name) {
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s/%s", nxc_test_out_dir(), name);
    return buf;
}

uint32_t nxc_test_pixel(const nxc_surface_t *s, int32_t x, int32_t y) {
    if (!s || x < 0 || y < 0 || x >= s->width || y >= s->height) return 0;
    uint32_t p = s->pixels[(size_t)y * (size_t)s->stride + (size_t)x];
    uint32_t a = p >> 24;
    if (a == 0) return 0;
    if (a == 255) return p;
    uint32_t r = ((p >> 16) & 0xFF) * 255u / a;
    uint32_t g = ((p >> 8) & 0xFF) * 255u / a;
    uint32_t b = (p & 0xFF) * 255u / a;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

int nxc_test_color_near(uint32_t a, uint32_t b, int tol) {
    for (int s = 0; s < 32; s += 8) {
        int ca = (int)((a >> s) & 0xFF), cb = (int)((b >> s) & 0xFF);
        if (ca - cb > tol || cb - ca > tol) return 0;
    }
    return 1;
}

static uint32_t crc_table[256];
static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}
static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    if (len) crc = crc_update(crc, data, len);
    crc ^= 0xFFFFFFFFu;
    uint8_t tail[4];
    put_be32(tail, crc);
    if (fwrite(hdr, 1, 8, f) != 8) return -1;
    if (len && fwrite(data, 1, len, f) != len) return -1;
    if (fwrite(tail, 1, 4, f) != 4) return -1;
    return 0;
}

int nxc_test_write_png(const nxc_surface_t *s, const char *path) {
    if (!s || !s->pixels || !path) return -1;
    static int crc_ready = 0;
    if (!crc_ready) { crc_init(); crc_ready = 1; }
    size_t w = (size_t)s->width, h = (size_t)s->height;
    size_t raw_len = h * (1 + w * 4);
    uint8_t *raw = malloc(raw_len);
    if (!raw) return -1;
    for (size_t y = 0; y < h; y++) {
        uint8_t *row = raw + y * (1 + w * 4);
        row[0] = 0;
        for (size_t x = 0; x < w; x++) {
            uint32_t p = nxc_test_pixel(s, (int32_t)x, (int32_t)y);
            row[1 + x * 4 + 0] = (uint8_t)(p >> 16);
            row[1 + x * 4 + 1] = (uint8_t)(p >> 8);
            row[1 + x * 4 + 2] = (uint8_t)p;
            row[1 + x * 4 + 3] = (uint8_t)(p >> 24);
        }
    }
    /* zlib stream: header, stored blocks of <= 65535 bytes, Adler-32 */
    size_t nblocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + nblocks * 5 + raw_len + 4;
    uint8_t *z = malloc(z_len);
    if (!z) { free(raw); return -1; }
    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    uint32_t a1 = 1, a2 = 0;
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        z[zp++] = (off + n >= raw_len) ? 1 : 0;
        z[zp++] = (uint8_t)n; z[zp++] = (uint8_t)(n >> 8);
        z[zp++] = (uint8_t)~n; z[zp++] = (uint8_t)(~n >> 8);
        memcpy(z + zp, raw + off, n);
        for (size_t i = 0; i < n; i++) { a1 = (a1 + raw[off + i]) % 65521; a2 = (a2 + a1) % 65521; }
        zp += n;
    }
    put_be32(z + zp, (a2 << 16) | a1); zp += 4;

    FILE *f = fopen(path, "wb");
    int rc = -1;
    if (f) {
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        uint8_t ihdr[13];
        put_be32(ihdr, (uint32_t)w); put_be32(ihdr + 4, (uint32_t)h);
        ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
        rc = (fwrite(sig, 1, 8, f) == 8 &&
              write_chunk(f, "IHDR", ihdr, 13) == 0 &&
              write_chunk(f, "IDAT", z, (uint32_t)zp) == 0 &&
              write_chunk(f, "IEND", NULL, 0) == 0) ? 0 : -1;
        if (fclose(f) != 0) rc = -1;
    }
    free(raw);
    free(z);
    return rc;
}

int nxc_test_finish(const char *name) {
    if (nxc_test_failures) {
        fprintf(stderr, "%s: %d check(s) FAILED\n", name, nxc_test_failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", name);
    return 0;
}
