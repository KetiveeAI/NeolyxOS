/*
 * Reolab - Public API Header
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef REOLAB_H
#define REOLAB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct ReoLabApp ReoLabApp;
typedef struct ReoLabEditor ReoLabEditor;
typedef struct ReoLabProject ReoLabProject;
typedef struct ReoLabBuffer ReoLabBuffer;

/* Application */
ReoLabApp* reolab_app_create(const char* title, int width, int height);
void reolab_app_destroy(ReoLabApp* app);
int reolab_app_run(ReoLabApp* app);
void reolab_app_quit(ReoLabApp* app);

/* File operations */
bool reolab_app_open_file(ReoLabApp* app, const char* path);
bool reolab_app_save_file(ReoLabApp* app);
bool reolab_app_save_file_as(ReoLabApp* app, const char* path);
bool reolab_app_close_file(ReoLabApp* app);

/* Project operations */
ReoLabProject* reolab_project_open(const char* path);
void reolab_project_close(ReoLabProject* project);
bool reolab_project_build(ReoLabProject* project);
bool reolab_project_run(ReoLabProject* project);

/* Editor operations */
ReoLabEditor* reolab_editor_get_active(ReoLabApp* app);
void reolab_editor_goto_line(ReoLabEditor* editor, int line);
void reolab_editor_find(ReoLabEditor* editor, const char* query);
void reolab_editor_replace(ReoLabEditor* editor, const char* find, const char* replace);

/* Buffer operations */
ReoLabBuffer* reolab_buffer_create(void);
void reolab_buffer_destroy(ReoLabBuffer* buffer);
void reolab_buffer_insert(ReoLabBuffer* buffer, size_t pos, const char* text);
void reolab_buffer_delete(ReoLabBuffer* buffer, size_t start, size_t end);
const char* reolab_buffer_get_text(ReoLabBuffer* buffer);
size_t reolab_buffer_get_length(ReoLabBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif /* REOLAB_H */
