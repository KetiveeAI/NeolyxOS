/*
 * Reolab - Text Buffer Implementation (Gap Buffer)
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_SIZE 4096
#define GAP_SIZE 256

ReoLabBuffer* reolab_buffer_create(void) {
    ReoLabBuffer* buf = (ReoLabBuffer*)calloc(1, sizeof(ReoLabBuffer));
    if (!buf) return NULL;
    
    buf->size = INITIAL_SIZE;
    buf->data = (char*)malloc(buf->size);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    
    buf->gap_start = 0;
    buf->gap_end = buf->size;
    
    buf->line_capacity = 1024;
    buf->line_starts = (size_t*)malloc(buf->line_capacity * sizeof(size_t));
    buf->line_starts[0] = 0;
    buf->line_count = 1;
    
    buf->undo_top = -1;
    buf->redo_top = -1;
    
    return buf;
}

void reolab_buffer_destroy(ReoLabBuffer* buf) {
    if (!buf) return;
    
    /* Free undo stack text */
    for (int i = 0; i <= buf->undo_top; i++) {
        free(buf->undo_stack[i].text);
    }
    
    free(buf->line_starts);
    free(buf->data);
    free(buf);
}

static size_t buffer_content_length(ReoLabBuffer* buf) {
    return buf->size - (buf->gap_end - buf->gap_start);
}

void buffer_move_gap(ReoLabBuffer* buf, size_t pos) {
    if (pos == buf->gap_start) return;
    
    size_t gap_size = buf->gap_end - buf->gap_start;
    
    if (pos < buf->gap_start) {
        /* Move gap left */
        size_t move_size = buf->gap_start - pos;
        memmove(buf->data + buf->gap_end - move_size, 
                buf->data + pos, move_size);
        buf->gap_start = pos;
        buf->gap_end = pos + gap_size;
    } else {
        /* Move gap right */
        size_t move_size = pos - buf->gap_start;
        memmove(buf->data + buf->gap_start, 
                buf->data + buf->gap_end, move_size);
        buf->gap_start = pos;
        buf->gap_end = pos + gap_size;
    }
}

void buffer_expand_gap(ReoLabBuffer* buf, size_t min_size) {
    size_t gap_size = buf->gap_end - buf->gap_start;
    if (gap_size >= min_size) return;
    
    size_t new_size = buf->size + min_size - gap_size + GAP_SIZE;
    char* new_data = (char*)malloc(new_size);
    
    /* Copy data before gap */
    memcpy(new_data, buf->data, buf->gap_start);
    
    /* Copy data after gap */
    size_t after_gap = buf->size - buf->gap_end;
    memcpy(new_data + new_size - after_gap, 
           buf->data + buf->gap_end, after_gap);
    
    free(buf->data);
    buf->data = new_data;
    buf->gap_end = new_size - after_gap;
    buf->size = new_size;
}

void reolab_buffer_insert(ReoLabBuffer* buf, size_t pos, const char* text) {
    if (!buf || !text) return;
    
    size_t len = strlen(text);
    if (len == 0) return;
    
    buffer_move_gap(buf, pos);
    buffer_expand_gap(buf, len);
    
    memcpy(buf->data + buf->gap_start, text, len);
    buf->gap_start += len;
    
    buffer_update_lines(buf);
}

void reolab_buffer_delete(ReoLabBuffer* buf, size_t start, size_t end) {
    if (!buf || start >= end) return;
    
    buffer_move_gap(buf, start);
    buf->gap_end += (end - start);
    
    buffer_update_lines(buf);
}

const char* reolab_buffer_get_text(ReoLabBuffer* buf) {
    if (!buf) return "";
    
    /* Move gap to end for contiguous text */
    buffer_move_gap(buf, buffer_content_length(buf));
    buf->data[buf->gap_start] = '\0';
    
    return buf->data;
}

size_t reolab_buffer_get_length(ReoLabBuffer* buf) {
    if (!buf) return 0;
    return buffer_content_length(buf);
}

void buffer_update_lines(ReoLabBuffer* buf) {
    buf->line_count = 1;
    buf->line_starts[0] = 0;
    
    size_t pos = 0;
    for (size_t i = 0; i < buf->gap_start; i++) {
        if (buf->data[i] == '\n') {
            if (buf->line_count >= buf->line_capacity) {
                buf->line_capacity *= 2;
                buf->line_starts = (size_t*)realloc(buf->line_starts, 
                    buf->line_capacity * sizeof(size_t));
            }
            buf->line_starts[buf->line_count++] = pos + 1;
        }
        pos++;
    }
    
    for (size_t i = buf->gap_end; i < buf->size; i++) {
        if (buf->data[i] == '\n') {
            if (buf->line_count >= buf->line_capacity) {
                buf->line_capacity *= 2;
                buf->line_starts = (size_t*)realloc(buf->line_starts, 
                    buf->line_capacity * sizeof(size_t));
            }
            buf->line_starts[buf->line_count++] = pos + 1;
        }
        pos++;
    }
}

size_t buffer_line_start(ReoLabBuffer* buf, size_t line) {
    if (!buf || line >= buf->line_count) return 0;
    return buf->line_starts[line];
}

size_t buffer_line_length(ReoLabBuffer* buf, size_t line) {
    if (!buf || line >= buf->line_count) return 0;
    
    size_t start = buf->line_starts[line];
    size_t end;
    
    if (line + 1 < buf->line_count) {
        end = buf->line_starts[line + 1] - 1; /* Exclude newline */
    } else {
        end = buffer_content_length(buf);
    }
    
    return end - start;
}
