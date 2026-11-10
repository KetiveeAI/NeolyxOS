/*
 * NeolyxOS - Files (Path) as its own process
 *
 * The Path view (shell/views/path_view.c, the system file browser) run by
 * the view host: /apps/Files [folder]. The window is titled "Path" so the
 * shell's dock and app drawer find it (the dock label "Files" maps to it).
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "../viewhost.h"
#include "app_view.h"
#include "nxsyscall.h"

static const viewhost_app_t k_files = {
    .title     = "Path",
    .width     = 860,
    .height    = 540,
    .resizable = 1,
    .open      = path_view_open,
    .render    = path_view_render,
    .key       = path_view_key,
    .mouse     = path_view_mouse,
    .scroll    = path_view_scroll,
    .open_path = path_view_open_at,
};

/* Entry: RDI = argc, RSI = argv (kernel/src/core/process.c). The kernel
 * enters with RSP 16-byte aligned and no return address, so realign. */
__attribute__((force_align_arg_pointer, noreturn))
void _start(int argc, char **argv) {
    int status = viewhost_run(&k_files, argc, argv);
    nx_exit(status);
    for (;;) __asm__ volatile("pause");
}
