/*
 * NeolyxOS - Shell-hosted app view registry (see app_view.h)
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "app_view.h"

static const app_view_t g_views[] = {
    { "System Settings", 860, 560, settings_view_open, settings_view_render, settings_view_key, settings_view_mouse },
    { "Font Manager",    760, 500, fonts_view_open,    fonts_view_render,    fonts_view_key,    fonts_view_mouse },
    { "Task Manager",    780, 520, taskmgr_view_open,  taskmgr_view_render,  taskmgr_view_key,  taskmgr_view_mouse },
    { "Path",            860, 540, path_view_open,     path_view_render,     path_view_key,     path_view_mouse },
    { "Shell",           720, 440, shell_view_open,    shell_view_render,    shell_view_key,    shell_view_mouse },
    { "Notes",           820, 520, notes_view_open,    notes_view_render,    notes_view_key,    notes_view_mouse },
};

static int view_streq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* Short names used by dock labels, Control Center and search. */
static const struct { const char *alias, *name; } g_aliases[] = {
    { "Settings", "System Settings" },
    { "Fonts",    "Font Manager" },
    { "Activity", "Task Manager" },
    { "Tasks",    "Task Manager" },
    { "Files",        "Path" },
    { "File Manager", "Path" },
    { "Finder",       "Path" },
    { "Terminal", "Shell" },        /* the app was called Terminal before */
    { "Console",  "Shell" },
    { "Note",     "Notes" },
    { "Notepad",  "Notes" },
    { "Text Editor", "Notes" },
};

const app_view_t *app_view_find(const char *name) {
    if (!name) return 0;
    for (unsigned i = 0; i < sizeof(g_aliases) / sizeof(g_aliases[0]); i++) {
        if (view_streq_ci(name, g_aliases[i].alias)) { name = g_aliases[i].name; break; }
    }
    for (unsigned i = 0; i < sizeof(g_views) / sizeof(g_views[0]); i++) {
        if (view_streq_ci(name, g_views[i].name)) return &g_views[i];
    }
    return 0;
}
