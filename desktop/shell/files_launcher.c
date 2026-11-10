/*
 * NeolyxOS - Files (Path) launcher, desktop side
 *
 * Path runs as its own process (/apps/Path, desktop/viewhost/files). The
 * desktop keeps the entry points other parts of the shell call:
 *
 *   path_view_open_at(path)       desktop icons, Notes "Show in Path": show
 *                                 a folder in the open Path window (window
 *                                 protocol 1.1 OPEN_PATH), or start Path
 *                                 there (argv[1])
 *   desktop_shell_open_path(path) SHELL_OPEN from an app (nxserver): a
 *                                 folder opens in Path, a text file in Notes
 *
 * Paths are normalized and checked against the file system here; an app's
 * path is never used as given.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "app_view.h"
#include "nxpath_relay.h"
#include "nxserver/nxserver.h"
#include "../../sdk/nxwindow/nxwin_proto.h"
#include "nxsyscall.h"

static void fl_log(const char *a, const char *b) {
    nx_debug_print("[FILES] ");
    nx_debug_print(a);
    if (b) { nx_debug_print(" "); nx_debug_print(b); }
    nx_debug_print("\n");
}

static int fl_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* The open Path window: a client window titled "Path" whose process the
 * shell started from /apps/Path. The title alone is not enough: any app can
 * call its window "Path", and must not be sent the folders the user opens. */
static uint32_t fl_path_window(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (!w || !w->id || !fl_streq(w->title, "Path")) continue;
        uint32_t pid = nxserver_window_pid(w->id);
        if (pid && desktop_app_started_pid("Path", pid)) return w->id;
    }
    return 0;
}

int path_view_open_at(const char *path) {
    char canon[NXRELAY_PATH_MAX];
    if (!path || nxrelay_normalize(path, canon, (int)sizeof(canon)) != NXRELAY_OK) {
        fl_log("open_at: rejected", path ? path : "(null)");
        return -1;
    }
    uint32_t id = fl_path_window();
    if (id) {
        int r = nxserver_open_path(id, canon);
        if (r == 0) {
            desktop_activate_window(id);
            return 0;
        }
        fl_log("open_at: the open Path window cannot take it now; new window at", canon);
    }
    /* No Path window (or it cannot take the request): a new one there */
    int r = desktop_start_app_with("Path", canon);
    if (r != 0) fl_log("open_at: could not start Path at", canon);
    return r == 0 ? 0 : -1;
}

int desktop_shell_open_path(const char *path) {
    char canon[NXRELAY_PATH_MAX];
    if (!path || nxrelay_normalize(path, canon, (int)sizeof(canon)) != NXRELAY_OK) return -NXW_EINVAL;
    nxrelay_kind_t kind;
    if (nxrelay_stat(canon, &kind, 0) != NXRELAY_OK) return -NXW_ENOENT_PATH;
    if (kind == NXRELAY_KIND_FOLDER) return path_view_open_at(canon) == 0 ? 0 : -NXW_EINVAL;
    if (kind == NXRELAY_KIND_FILE && nxrelay_is_text_name(canon)) {
        return notes_view_open_file(canon) == 0 ? 0 : -NXW_EINVAL;
    }
    fl_log("no app opens", canon);
    return -NXW_ENOEXEC;
}
