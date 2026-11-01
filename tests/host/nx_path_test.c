/*
 * Host unit test for path normalisation (kernel/src/core/nx_path.c),
 * section 4.2 of docs/kernel/proposals/beta-process-abi.md: relative paths
 * are joined to the working directory, ".", ".." and repeated "/" are
 * resolved, ".." never climbs above "/", and the 255-byte limit is an
 * error, never a silent cut.
 *
 * Build and run from the repository root:
 *   gcc -std=c11 -O1 -Wall -Wextra -Ikernel/include \
 *       -o /tmp/nx_path_test tests/host/nx_path_test.c kernel/src/core/nx_path.c
 *   /tmp/nx_path_test
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdio.h>
#include <string.h>

#include "core/nx_fdtab.h"

static int failures;
static int checks;

static void expect(const char *cwd, const char *path, const char *want) {
    char out[NX_PATH_MAX];
    checks++;
    int rc = nx_path_normalize(cwd, path, out, sizeof(out));
    if (rc < 0 || strcmp(out, want) != 0 || (size_t)rc != strlen(want)) {
        failures++;
        printf("FAIL cwd='%s' path='%s': got rc=%d '%s', want '%s'\n",
               cwd ? cwd : "(null)", path, rc, rc < 0 ? "" : out, want);
    }
}

static void expect_err(const char *cwd, const char *path, uint32_t cap, int want) {
    char out[NX_PATH_MAX * 2];
    checks++;
    int rc = nx_path_normalize(cwd, path, out, cap);
    if (rc != want) {
        failures++;
        printf("FAIL cwd='%s' path='%.40s...': got %d, want %d\n", cwd ? cwd : "(null)", path, rc, want);
    }
}

int main(void) {
    expect("/", "/", "/");
    expect("/", "/apps", "/apps");
    expect("/", "/apps/", "/apps");
    expect("/", "//apps///ticker", "/apps/ticker");
    expect("/", "/apps/./ticker", "/apps/ticker");
    expect("/", "/apps/../System", "/System");
    expect("/", "/..", "/");
    expect("/", "/../../x", "/x");
    expect("/apps", "ticker", "/apps/ticker");
    expect("/apps", "./ticker", "/apps/ticker");
    expect("/apps", "..", "/");
    expect("/apps", "../System/Config", "/System/Config");
    expect("/Users/a/b", "../../c", "/Users/c");
    expect("/Users/a/b", "../../../../..", "/");
    expect("/apps", ".", "/apps");
    expect("/apps", "/abs", "/abs");
    expect("/", "a/b/../c/./d", "/a/c/d");
    expect("/", "...", "/...");
    expect("/", ".hidden", "/.hidden");
    expect(NULL, "rel", "/rel");               /* no cwd: from "/" */
    expect("bad", "rel", "/rel");              /* cwd not absolute: from "/" */

    /* Errors */
    expect_err("/", "", NX_PATH_MAX, -2);                  /* empty: -ENOENT */
    expect_err("/", NULL, NX_PATH_MAX, -22);
    char longp[400];
    memset(longp, 'a', sizeof(longp));
    longp[0] = '/';
    longp[256] = '\0';                                     /* 256 bytes: too long */
    expect_err("/", longp, NX_PATH_MAX, -36);
    longp[255] = '\0';                                     /* 255 bytes: fits */
    expect_err("/", longp, NX_PATH_MAX, 255);
    /* A relative path that only overflows once joined to the cwd */
    char cwd[200];
    memset(cwd, 'c', sizeof(cwd));
    cwd[0] = '/';
    cwd[199] = '\0';
    char rel[100];
    memset(rel, 'r', sizeof(rel));
    rel[99] = '\0';
    expect_err(cwd, rel, NX_PATH_MAX, -36);
    /* ".." can shorten a path that would otherwise be too long */
    char back[420];
    snprintf(back, sizeof(back), "%s/../x", longp);        /* 255 + "/../x" */
    expect_err("/", back, NX_PATH_MAX, 2);

    printf("nx_path_test: %d checks, %d failures\n", checks, failures);
    printf("%s\n", failures ? "RESULT: FAIL" : "RESULT: PASS");
    return failures ? 1 : 0;
}
