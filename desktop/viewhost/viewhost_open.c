/*
 * NeolyxOS - view host: handing a path to the shell
 *
 * A hosted view cannot open another app itself. Views that would call the
 * shell's open functions (Path opening a text file in Notes) get these
 * versions, which ask the shell through the window protocol.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "viewhost.h"
#include "app_view.h"
#include "nxsyscall.h"

/* Path: "open this text file in Notes". The shell opens Notes (it still
 * runs inside the desktop) and saves the note open there first. */
int notes_view_open_file(const char *path) {
    if (!path || path[0] != '/') return -1;
    return viewhost_shell_open(path);
}
