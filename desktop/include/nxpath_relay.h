/*
 * NeolyxOS - Path relay: the desktop's file-system broker
 *
 * Every part of the desktop that shows files (the Path app, the desktop
 * surface, open/save panels, search) asks the relay instead of calling
 * the kernel directly. The relay
 *
 *   - validates and canonicalizes every path (absolute, no "..", no
 *     control characters, bounded length) before it reaches the kernel;
 *   - lists folders through SYS_READDIR (30) and SYS_STAT (25), sorts
 *     them (folders first, then by name) and caches the result, so the
 *     kernel is asked once per navigation, not once per frame;
 *   - resolves the standard places (Home, Desktop, Documents, ...) for
 *     the signed-in user;
 *   - bumps a generation counter whenever a listing changes, so views
 *     can tell when to redraw;
 *   - logs every failure with the path and the kernel's error.
 *
 * The relay runs inside the desktop process (userspace). Its only write
 * is creating the signed-in user's standard home folders
 * (nxrelay_ensure_home); moving, renaming and deleting will be added
 * here, behind the same validation, when the kernel exposes them.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXPATH_RELAY_H
#define NXPATH_RELAY_H

#include <stdint.h>

#define NXRELAY_NAME_MAX     255     /* matches the kernel's VFS_NAME_MAX */
#define NXRELAY_PATH_MAX     512     /* longest path the relay accepts */
#define NXRELAY_MAX_ENTRIES  256     /* entries kept per listing */

/* Errors (negative). The kernel's VFS errors are mapped onto these. */
#define NXRELAY_OK              0
#define NXRELAY_ERR_INVALID   (-1)   /* bad argument or unsafe path */
#define NXRELAY_ERR_NOT_FOUND (-2)
#define NXRELAY_ERR_NOT_DIR   (-3)
#define NXRELAY_ERR_DENIED    (-4)
#define NXRELAY_ERR_IO        (-5)
#define NXRELAY_ERR_NO_KERNEL (-6)   /* the kernel has no such interface */

typedef enum {
    NXRELAY_KIND_FILE   = 1,
    NXRELAY_KIND_FOLDER = 2,
    NXRELAY_KIND_LINK   = 3,
    NXRELAY_KIND_DEVICE = 4,
} nxrelay_kind_t;

typedef struct {
    char     name[NXRELAY_NAME_MAX + 1];
    uint8_t  kind;          /* nxrelay_kind_t */
    uint8_t  hidden;        /* name starts with '.' */
    uint8_t  size_known;    /* size came from the kernel */
    uint64_t size;          /* bytes (files) */
} nxrelay_entry_t;

typedef struct {
    char                   path[NXRELAY_PATH_MAX];   /* canonical */
    int                    status;      /* NXRELAY_OK or an error */
    uint32_t               count;       /* entries[] in use */
    int                    truncated;   /* more entries than NXRELAY_MAX_ENTRIES */
    uint32_t               generation;  /* changes when this listing is reloaded */
    uint64_t               loaded_ms;   /* monotonic time of the last load */
    int                    synthesized; /* "/" built from known places (see below) */
    const nxrelay_entry_t *entries;
} nxrelay_listing_t;

/* Listing flags */
#define NXRELAY_REFRESH      0x1u    /* bypass the cache and ask the kernel */
#define NXRELAY_SHOW_HIDDEN  0x2u    /* include dot files */

/* Standard places */
typedef enum {
    NXRELAY_LOC_HOME = 0,
    NXRELAY_LOC_DESKTOP,
    NXRELAY_LOC_DOCUMENTS,
    NXRELAY_LOC_DOWNLOADS,
    NXRELAY_LOC_PICTURES,
    NXRELAY_LOC_MUSIC,
    NXRELAY_LOC_APPLICATIONS,
    NXRELAY_LOC_SYSTEM,
    NXRELAY_LOC_VOLUMES,
    NXRELAY_LOC_ROOT,
    NXRELAY_LOC_COUNT
} nxrelay_loc_t;

/* Set the signed-in user (home becomes /Users/<name>). Names are
 * validated: 1..31 chars of [A-Za-z0-9._-], not starting with '.'.
 * Returns NXRELAY_OK or NXRELAY_ERR_INVALID. */
int nxrelay_set_user(const char *name);

/* Create /Users/<name> and its standard folders (Desktop, Documents,
 * Downloads, Pictures, Music) where missing. Existing folders are left
 * alone. Returns NXRELAY_OK when every folder exists afterwards. */
int nxrelay_ensure_home(void);

/* Canonicalize `in` into `out`: absolute, single separators, "." removed.
 * ".." components, control characters and over-long paths are rejected
 * (NXRELAY_ERR_INVALID); the relay never walks above a path it was given. */
int nxrelay_normalize(const char *in, char *out, int cap);

/* dir + "/" + name, validated. name must be a single component. */
int nxrelay_join(const char *dir, const char *name, char *out, int cap);

/* Parent folder of a canonical path ("/" stays "/"). */
int nxrelay_parent(const char *path, char *out, int cap);

/* Last component of a canonical path ("/" for the root). */
const char *nxrelay_basename(const char *path);

/*
 * List a folder. The result points into relay-owned storage and stays
 * valid until the next nxrelay_list call that evicts it (the relay keeps
 * a few recent listings). Never NULL: on failure, status holds the error
 * and count is 0.
 *
 * When the kernel cannot list "/" (no file system is mounted at the root
 * yet), the relay answers with the standard top-level folders that do
 * exist (each confirmed with SYS_STAT) and sets `synthesized`.
 */
const nxrelay_listing_t *nxrelay_list(const char *path, uint32_t flags);

/* Whether a path exists and what it is. kind/size may be NULL. */
int nxrelay_stat(const char *path, nxrelay_kind_t *kind, uint64_t *size);

/* Drop cached listings for path (NULL = all) so the next list reloads. */
void nxrelay_invalidate(const char *path);

/* Path of a standard place for the current user. Returns NXRELAY_OK even
 * when the folder does not exist yet; use nxrelay_stat to check. */
int nxrelay_location(nxrelay_loc_t loc, char *out, int cap);
/* Display name of a standard place ("Desktop"). */
const char *nxrelay_location_name(nxrelay_loc_t loc);

/* Short, user-facing description of an error code. */
const char *nxrelay_strerror(int err);

/* Increases whenever any listing is (re)loaded. */
uint32_t nxrelay_generation(void);

/* Self-test of the path rules (no kernel access). Returns failures. */
int nxrelay_selftest(void);

#endif /* NXPATH_RELAY_H */
