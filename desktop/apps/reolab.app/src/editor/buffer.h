/*
 * Reolab - Text Buffer (Gap Buffer Implementation)
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef REOLAB_EDITOR_BUFFER_H
#define REOLAB_EDITOR_BUFFER_H

#include "../../include/reolab.h"
#include <stddef.h>

/* Gap buffer for efficient text editing */
struct ReoLabBuffer {
    char* data;           /* Buffer data with gap */
    size_t size;          /* Total buffer capacity */
    size_t gap_start;     /* Start of gap */
    size_t gap_end;       /* End of gap */
    
    /* Line tracking */
    size_t* line_starts;  /* Array of line start positions */
    size_t line_count;    /* Number of lines */
    size_t line_capacity; /* Capacity of line_starts array */
    
    /* Undo stack */
    struct UndoEntry {
        enum { UNDO_INSERT, UNDO_DELETE } type;
        size_t position;
        char* text;
        size_t length;
    } undo_stack[1024];
    int undo_top;
    int redo_top;
};

/* Buffer internal operations */
void buffer_move_gap(ReoLabBuffer* buf, size_t pos);
void buffer_expand_gap(ReoLabBuffer* buf, size_t min_size);
void buffer_update_lines(ReoLabBuffer* buf);
size_t buffer_line_length(ReoLabBuffer* buf, size_t line);
size_t buffer_line_start(ReoLabBuffer* buf, size_t line);

#endif /* REOLAB_EDITOR_BUFFER_H */
