/*
 * NeolyxOS - Installed font registry (userspace)
 *
 * One list of every typeface the desktop can use:
 *   - system fonts: the NX Sans family linked into the desktop image
 *     (registered by NXUI at startup, always enabled, cannot be removed);
 *   - user fonts: TrueType files in the user fonts directory
 *     (NXFONT_USER_DIR), listed in its index file and loaded through the
 *     nx_open / nx_read syscall wrappers.
 *
 * Font files are hostile input. Every user font is size checked, then
 * parsed by the NXCanvas TrueType engine, which validates the table
 * directory, every table offset and length, glyph and cmap counts and
 * all outline data before the face is accepted. Rejections carry a
 * human-readable reason and are logged.
 *
 * Index file format (NXFONT_USER_DIR "/" NXFONT_INDEX_NAME), plain text,
 * one font per line, '#' starts a comment:
 *     <enabled: 0|1> <file name>
 * File names are bare names (no directories) of [A-Za-z0-9._-] ending in
 * ".ttf". The kernel has no directory listing wrapper in nxsyscall.h yet,
 * so the index is the source of truth (see docs/desktop/fonts.md).
 *
 * Threading: not thread-safe; the desktop shell is single threaded.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXFONT_REGISTRY_H
#define NXFONT_REGISTRY_H

#include <stdint.h>
#include "nxcanvas.h"

#define NXFONT_MAX_FONTS      32            /* system + user faces */
#define NXFONT_MAX_USER_FONTS 24
#define NXFONT_MAX_FILE_BYTES (8u << 20)    /* largest font file accepted */
#define NXFONT_MIN_FILE_BYTES 256u
#define NXFONT_PATH_MAX       192
#define NXFONT_REASON_MAX     160
#define NXFONT_USER_DIR       "/Library/Fonts"
#define NXFONT_INDEX_NAME     "fonts.index"
#define NXFONT_INDEX_MAX      4096u         /* bytes of index file read */

typedef enum {
    NXFONT_SOURCE_SYSTEM = 0,
    NXFONT_SOURCE_USER   = 1
} nxfont_source_t;

typedef struct {
    uint32_t        id;                     /* stable handle, never 0 */
    char            family[64];             /* family shown and matched */
    char            style[48];              /* "Regular", "Semibold", ... */
    char            role[16];               /* "text", "ui", "display", "mono", "user" */
    char            location[NXFONT_PATH_MAX];  /* "Built in" or the file path */
    nxfont_source_t source;
    uint8_t         enabled;                /* disabled fonts are not returned by lookups */
    uint8_t         locked;                 /* 1 = cannot be disabled (system fonts) */
    uint8_t         persisted;              /* user font: 1 = recorded in the index file */
    uint8_t         reserved;
    nxc_font_info_t info;                   /* from the font file itself */
} nxfont_entry_t;

/* Reset the registry (drops user fonts; system fonts must be re-added). */
nxc_status_t nxfont_registry_init(void);

/* Register a system face that the caller keeps alive for the program's
 * lifetime. family/style/role are display names (e.g. "NX Sans",
 * "Semibold", "display"). Returns the new id in *id_out (may be NULL). */
nxc_status_t nxfont_registry_add_system(const nxc_font_t *face, const char *family,
                                        const char *style, const char *role, uint32_t *id_out);

/* Load the user fonts listed in <dir>/fonts.index (dir NULL = default).
 * Each bad entry is skipped with a logged reason. Returns NXC_OK when the
 * index was read (even if some fonts were rejected), NXC_ERR_NOT_FOUND
 * when there is no index. *loaded_out (may be NULL) receives the count. */
nxc_status_t nxfont_registry_load_user(const char *dir, uint32_t *loaded_out);

/* Read and validate a font file without installing it. On success *info
 * is filled; on failure reason (cap >= 1) explains why. */
nxc_status_t nxfont_validate_file(const char *path, nxc_font_info_t *info,
                                  char *reason, uint32_t reason_cap);

/*
 * Validate and install the font at `path`: the file is checked, loaded,
 * copied into the user fonts directory (unless it is already there) and
 * added to the index. When the copy or index write fails the font stays
 * installed for this session only (entry->persisted = 0) and `message`
 * says so; the status is still NXC_OK. Any validation failure returns an
 * error with the reason in `message`.
 */
nxc_status_t nxfont_install(const char *path, uint32_t *id_out, char *message,
                            uint32_t message_cap);

/* Enable or disable a user font (persisted in the index when possible).
 * System fonts are locked: NXC_ERR_UNSUPPORTED. */
nxc_status_t nxfont_set_enabled(uint32_t id, int enabled);

/* Enumeration (index 0 .. count-1, registration order). */
uint32_t nxfont_count(void);
const nxfont_entry_t *nxfont_at(uint32_t index);
const nxfont_entry_t *nxfont_get(uint32_t id);
/* The loaded face for an entry (NULL for an unknown id). Disabled fonts
 * still return their face here so a font manager can preview them. */
const nxc_font_t *nxfont_face(uint32_t id);

/* Families in first-registration order. */
uint32_t nxfont_family_count(void);
const char *nxfont_family_at(uint32_t index);
/* Ids of a family's faces (enabled or not), sorted by weight then
 * upright before italic. Returns the number written (<= cap). */
uint32_t nxfont_family_styles(const char *family, uint32_t *ids, uint32_t cap);

/*
 * Best enabled face of a family (case-insensitive) for a weight
 * (100..900) and slant, using the CSS font matching order: exact weight;
 * for 400 try 500 next, for 500 try 400 next; below 400 lighter weights
 * first then heavier; above 500 heavier first then lighter. An upright
 * face is used when no italic exists. NULL when the family has no enabled
 * face.
 */
const nxc_font_t *nxfont_lookup(const char *family, uint32_t weight, int italic);

/* Number of code points a face maps (computed once and cached). */
uint32_t nxfont_char_count(uint32_t id);

#endif /* NXFONT_REGISTRY_H */
