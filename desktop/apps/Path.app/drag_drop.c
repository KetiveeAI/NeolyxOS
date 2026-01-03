/*
 * NeolyxOS Path.app - Drag and Drop
 * 
 * File dragging and dropping support for moving/copying files.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"

/* ============ Constants ============ */

#define DRAG_MAX_FILES      16
#define DRAG_PATH_MAX       1024
#define DRAG_THRESHOLD      5   /* Pixels to move before drag starts */

/* ============ Colors ============ */

#define COLOR_DRAG_BG       0x80313244   /* Semi-transparent */
#define COLOR_DRAG_BORDER   0x89B4FA
#define COLOR_DRAG_TEXT     0xCDD6F4
#define COLOR_DROP_VALID    0x4089B4FA   /* Green tint */
#define COLOR_DROP_INVALID  0x40F38BA8   /* Red tint */

/* ============ Drag State ============ */

typedef enum {
    DRAG_NONE,
    DRAG_PENDING,    /* Mouse down, waiting for threshold */
    DRAG_ACTIVE      /* Dragging in progress */
} drag_mode_t;

typedef enum {
    DROP_NONE,
    DROP_COPY,
    DROP_MOVE,
    DROP_LINK
} drop_action_t;

typedef struct {
    drag_mode_t mode;
    
    /* Source files */
    char source_paths[DRAG_MAX_FILES][DRAG_PATH_MAX];
    char source_names[DRAG_MAX_FILES][256];
    int file_count;
    
    /* Drag origin (for threshold check) */
    int start_x;
    int start_y;
    
    /* Current drag position */
    int drag_x;
    int drag_y;
    
    /* Drop target */
    char target_path[DRAG_PATH_MAX];
    int drop_valid;
    drop_action_t drop_action;
    
    /* Visual offset */
    int offset_x;
    int offset_y;
} drag_state_t;

/* ============ Initialization ============ */

void drag_init(drag_state_t *drag) {
    drag->mode = DRAG_NONE;
    drag->file_count = 0;
    drag->start_x = 0;
    drag->start_y = 0;
    drag->drag_x = 0;
    drag->drag_y = 0;
    drag->target_path[0] = '\0';
    drag->drop_valid = 0;
    drag->drop_action = DROP_NONE;
    drag->offset_x = 8;
    drag->offset_y = 8;
}

/* ============ Drag Operations ============ */

/*
 * Start a potential drag operation (on mouse down)
 */
void drag_begin(drag_state_t *drag, int x, int y) {
    drag->mode = DRAG_PENDING;
    drag->start_x = x;
    drag->start_y = y;
    drag->drag_x = x;
    drag->drag_y = y;
    drag->file_count = 0;
}

/*
 * Add a file to the drag operation
 */
int drag_add_file(drag_state_t *drag, const char *path, const char *name) {
    if (drag->file_count >= DRAG_MAX_FILES) return -1;
    
    int i = 0;
    while (path[i] && i < DRAG_PATH_MAX - 1) {
        drag->source_paths[drag->file_count][i] = path[i];
        i++;
    }
    drag->source_paths[drag->file_count][i] = '\0';
    
    i = 0;
    while (name[i] && i < 255) {
        drag->source_names[drag->file_count][i] = name[i];
        i++;
    }
    drag->source_names[drag->file_count][i] = '\0';
    
    drag->file_count++;
    return 0;
}

/*
 * Update drag position (on mouse move)
 * Returns 1 if drag became active
 */
int drag_update(drag_state_t *drag, int x, int y) {
    drag->drag_x = x;
    drag->drag_y = y;
    
    if (drag->mode == DRAG_PENDING) {
        /* Check threshold */
        int dx = x - drag->start_x;
        int dy = y - drag->start_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        
        if (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD) {
            drag->mode = DRAG_ACTIVE;
            return 1;
        }
    }
    
    return 0;
}

/*
 * Set drop target (when hovering over a folder)
 */
void drag_set_target(drag_state_t *drag, const char *path, int valid) {
    if (path) {
        int i = 0;
        while (path[i] && i < DRAG_PATH_MAX - 1) {
            drag->target_path[i] = path[i];
            i++;
        }
        drag->target_path[i] = '\0';
    } else {
        drag->target_path[0] = '\0';
    }
    drag->drop_valid = valid;
}

/*
 * Set the drop action based on modifier keys
 */
void drag_set_action(drag_state_t *drag, int ctrl_held, int alt_held) {
    if (alt_held) {
        drag->drop_action = DROP_LINK;
    } else if (ctrl_held) {
        drag->drop_action = DROP_COPY;
    } else {
        drag->drop_action = DROP_MOVE;
    }
}

/*
 * Check if drag is active
 */
int drag_is_active(drag_state_t *drag) {
    return drag->mode == DRAG_ACTIVE;
}

/*
 * Cancel the drag operation
 */
void drag_cancel(drag_state_t *drag) {
    drag->mode = DRAG_NONE;
    drag->file_count = 0;
    drag->target_path[0] = '\0';
    drag->drop_valid = 0;
}

/*
 * Complete the drag operation (on mouse up)
 * Returns the action to perform, or DROP_NONE if cancelled
 */
drop_action_t drag_end(drag_state_t *drag) {
    if (drag->mode != DRAG_ACTIVE || !drag->drop_valid) {
        drag_cancel(drag);
        return DROP_NONE;
    }
    
    drop_action_t action = drag->drop_action;
    drag->mode = DRAG_NONE;
    
    return action;
}

/* ============ Drawing ============ */

void drag_draw(void *ctx, drag_state_t *drag) {
    if (drag->mode != DRAG_ACTIVE || drag->file_count == 0) return;
    
    int x = drag->drag_x + drag->offset_x;
    int y = drag->drag_y + drag->offset_y;
    
    /* Draw drag indicator */
    int width = 160;
    int height = 24;
    
    if (drag->file_count > 1) {
        height = 36;  /* Extra space for count */
    }
    
    /* Background with shadow */
    nx_fill_rect(ctx, x + 2, y + 2, width, height, 0x11111B);  /* Shadow */
    nx_fill_rect(ctx, x, y, width, height, COLOR_DRAG_BG);
    nx_draw_rect(ctx, x, y, width, height, COLOR_DRAG_BORDER);
    
    /* File icon placeholder */
    nx_fill_rect(ctx, x + 8, y + 6, 12, 12, COLOR_DRAG_BORDER);
    
    /* Filename */
    if (drag->file_count == 1) {
        nx_draw_text(ctx, drag->source_names[0], x + 28, y + 6, COLOR_DRAG_TEXT);
    } else {
        /* "X items" */
        char count_str[32];
        int i = 0;
        int num = drag->file_count;
        char tmp[8];
        int j = 0;
        while (num > 0) {
            tmp[j++] = '0' + (num % 10);
            num /= 10;
        }
        while (j > 0) count_str[i++] = tmp[--j];
        const char *suffix = " items";
        for (j = 0; suffix[j]; j++) count_str[i++] = suffix[j];
        count_str[i] = '\0';
        
        nx_draw_text(ctx, count_str, x + 28, y + 6, COLOR_DRAG_TEXT);
        
        /* First filename preview */
        nx_draw_text(ctx, drag->source_names[0], x + 28, y + 20, 0x6C7086);
    }
    
    /* Action indicator */
    const char *action_str = 0;
    switch (drag->drop_action) {
        case DROP_COPY: action_str = "+"; break;
        case DROP_LINK: action_str = "@"; break;
        default: break;
    }
    
    if (action_str) {
        nx_fill_rect(ctx, x + width - 20, y + height - 16, 14, 14, COLOR_DRAG_BORDER);
        nx_draw_text(ctx, action_str, x + width - 17, y + height - 14, 0x1E1E2E);
    }
}

/*
 * Draw drop zone highlight
 */
void drag_draw_drop_zone(void *ctx, drag_state_t *drag, 
                         int zone_x, int zone_y, int zone_w, int zone_h) {
    if (drag->mode != DRAG_ACTIVE) return;
    
    uint32_t color = drag->drop_valid ? COLOR_DROP_VALID : COLOR_DROP_INVALID;
    
    /* Semi-transparent overlay */
    nx_fill_rect(ctx, zone_x, zone_y, zone_w, zone_h, color);
    
    /* Border */
    uint32_t border = drag->drop_valid ? 0x89B4FA : 0xF38BA8;
    nx_draw_rect(ctx, zone_x, zone_y, zone_w, zone_h, border);
}
