/*
 * NeolyxOS path normalisation (kernel-internal, no kernel dependencies).
 * See nx_path_normalize() in core/nx_fdtab.h. Host test:
 * tests/host/nx_path_test.c.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/nx_fdtab.h"

#define P_ENOENT        2
#define P_EINVAL        22
#define P_ENAMETOOLONG  36

/* Feed one '/'-separated string into out (len is the current length) */
static int feed(const char *s, char *out, uint32_t cap, uint32_t *len) {
    uint32_t i = 0;
    while (s[i]) {
        while (s[i] == '/') i++;
        if (!s[i]) break;
        uint32_t start = i;
        while (s[i] && s[i] != '/') i++;
        uint32_t clen = i - start;

        if (clen == 1 && s[start] == '.') continue;
        if (clen == 2 && s[start] == '.' && s[start + 1] == '.') {
            /* Drop the last component; never above "/" */
            uint32_t l = *len;
            while (l > 1 && out[l - 1] != '/') l--;
            if (l > 1) l--;                 /* the separator itself */
            *len = l;
            out[l] = '\0';
            continue;
        }

        uint32_t need = *len + (*len > 1 ? 1 : 0) + clen;
        if (need + 1 > cap) return -P_ENAMETOOLONG;
        if (*len > 1) out[(*len)++] = '/';
        for (uint32_t k = 0; k < clen; k++) out[(*len)++] = s[start + k];
        out[*len] = '\0';
    }
    return 0;
}

int nx_path_normalize(const char *cwd, const char *path, char *out, uint32_t cap) {
    if (!path || !out || cap < 2) return -P_EINVAL;
    if (path[0] == '\0') return -P_ENOENT;

    uint32_t len = 1;
    out[0] = '/';
    out[1] = '\0';

    if (path[0] != '/' && cwd && cwd[0] == '/') {
        int rc = feed(cwd, out, cap, &len);
        if (rc != 0) return rc;
    }
    int rc = feed(path, out, cap, &len);
    if (rc != 0) return rc;
    return (int)len;
}
