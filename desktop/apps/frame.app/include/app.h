#ifndef APP_H
#define APP_H

#include <nximage.h>
#include <nxgfx/nxgfx.h>
#include <stdbool.h>

typedef struct {
    nximage_t current_image;
    nx_image_t* gfx_image;
    bool has_image;
} frame_app_t;

void frame_app_init(frame_app_t* app);
void frame_app_load_image(frame_app_t* app, const char* path);
void frame_app_render(frame_app_t* app, nx_context_t* ctx);

#endif
