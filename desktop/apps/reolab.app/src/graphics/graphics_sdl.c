/*
 * Reolab - Graphics Backend (SDL2 Implementation)
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "graphics.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct RLGraphics {
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    TTF_Font* font_small;
    TTF_Font* font_large;
    int width, height;
    bool quit;
    float mouse_x, mouse_y;
    uint32_t mouse_state;
    char text_input[256];
};

RLGraphics* rl_graphics_create(const char* title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "[Graphics] SDL init failed: %s\n", SDL_GetError());
        return NULL;
    }
    
    if (TTF_Init() < 0) {
        fprintf(stderr, "[Graphics] TTF init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return NULL;
    }
    
    RLGraphics* gfx = (RLGraphics*)calloc(1, sizeof(RLGraphics));
    gfx->width = width;
    gfx->height = height;
    gfx->quit = false;
    
    gfx->window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!gfx->window) {
        fprintf(stderr, "[Graphics] Window creation failed\n");
        free(gfx);
        return NULL;
    }
    
    gfx->renderer = SDL_CreateRenderer(gfx->window, -1, 
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    
    if (!gfx->renderer) {
        SDL_DestroyWindow(gfx->window);
        free(gfx);
        return NULL;
    }
    
    SDL_SetRenderDrawBlendMode(gfx->renderer, SDL_BLENDMODE_BLEND);
    SDL_StartTextInput();
    
    /* Load fonts */
    const char* font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        NULL
    };
    
    for (int i = 0; font_paths[i]; i++) {
        gfx->font = TTF_OpenFont(font_paths[i], 14);
        if (gfx->font) {
            gfx->font_small = TTF_OpenFont(font_paths[i], 11);
            gfx->font_large = TTF_OpenFont(font_paths[i], 18);
            break;
        }
    }
    
    if (!gfx->font) {
        fprintf(stderr, "[Graphics] No monospace font found\n");
    }
    
    printf("[Graphics] Initialized %dx%d\n", width, height);
    return gfx;
}

void rl_graphics_destroy(RLGraphics* gfx) {
    if (!gfx) return;
    
    if (gfx->font) TTF_CloseFont(gfx->font);
    if (gfx->font_small) TTF_CloseFont(gfx->font_small);
    if (gfx->font_large) TTF_CloseFont(gfx->font_large);
    
    SDL_StopTextInput();
    if (gfx->renderer) SDL_DestroyRenderer(gfx->renderer);
    if (gfx->window) SDL_DestroyWindow(gfx->window);
    
    TTF_Quit();
    SDL_Quit();
    free(gfx);
    
    printf("[Graphics] Destroyed\n");
}

void rl_graphics_begin_frame(RLGraphics* gfx) {
    if (!gfx) return;
    gfx->text_input[0] = '\0';
}

void rl_graphics_end_frame(RLGraphics* gfx) {
    if (!gfx) return;
    SDL_RenderPresent(gfx->renderer);
}

bool rl_graphics_poll_events(RLGraphics* gfx) {
    if (!gfx) return false;
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                gfx->quit = true;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                    gfx->quit = true;
                break;
            case SDL_MOUSEMOTION:
                gfx->mouse_x = (float)event.motion.x;
                gfx->mouse_y = (float)event.motion.y;
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                gfx->mouse_state = SDL_GetMouseState(NULL, NULL);
                break;
            case SDL_TEXTINPUT:
                strncat(gfx->text_input, event.text.text, 
                    sizeof(gfx->text_input) - strlen(gfx->text_input) - 1);
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    gfx->width = event.window.data1;
                    gfx->height = event.window.data2;
                }
                break;
        }
    }
    
    return !gfx->quit;
}

bool rl_graphics_should_quit(RLGraphics* gfx) {
    return gfx ? gfx->quit : true;
}

void rl_graphics_clear(RLGraphics* gfx, RLColor color) {
    if (!gfx) return;
    SDL_SetRenderDrawColor(gfx->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(gfx->renderer);
}

void rl_graphics_fill_rect(RLGraphics* gfx, RLRect rect, RLColor color) {
    if (!gfx) return;
    SDL_SetRenderDrawColor(gfx->renderer, color.r, color.g, color.b, color.a);
    SDL_FRect r = {rect.x, rect.y, rect.w, rect.h};
    SDL_RenderFillRectF(gfx->renderer, &r);
}

void rl_graphics_fill_rounded_rect(RLGraphics* gfx, RLRect rect, RLColor color, float radius) {
    if (!gfx) return;
    SDL_SetRenderDrawColor(gfx->renderer, color.r, color.g, color.b, color.a);
    
    if (radius <= 0) {
        SDL_FRect r = {rect.x, rect.y, rect.w, rect.h};
        SDL_RenderFillRectF(gfx->renderer, &r);
        return;
    }
    
    float r = fminf(radius, fminf(rect.w/2, rect.h/2));
    float x = rect.x, y = rect.y, w = rect.w, h = rect.h;
    
    SDL_FRect center = {x + r, y, w - 2*r, h};
    SDL_RenderFillRectF(gfx->renderer, &center);
    SDL_FRect left = {x, y + r, r, h - 2*r};
    SDL_RenderFillRectF(gfx->renderer, &left);
    SDL_FRect right = {x + w - r, y + r, r, h - 2*r};
    SDL_RenderFillRectF(gfx->renderer, &right);
    
    /* Draw corners */
    for (int cy = 0; cy < (int)r; cy++) {
        for (int cx = 0; cx < (int)r; cx++) {
            float dx = r - cx - 0.5f, dy = r - cy - 0.5f;
            if (dx*dx + dy*dy <= r*r) {
                SDL_RenderDrawPointF(gfx->renderer, x + cx, y + cy);
                SDL_RenderDrawPointF(gfx->renderer, x + w - r + cx, y + cy);
                SDL_RenderDrawPointF(gfx->renderer, x + cx, y + h - r + cy);
                SDL_RenderDrawPointF(gfx->renderer, x + w - r + cx, y + h - r + cy);
            }
        }
    }
}

void rl_graphics_draw_line(RLGraphics* gfx, float x1, float y1, float x2, float y2, RLColor color) {
    if (!gfx) return;
    SDL_SetRenderDrawColor(gfx->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLineF(gfx->renderer, x1, y1, x2, y2);
}

void rl_graphics_draw_text(RLGraphics* gfx, const char* text, float x, float y, float size, RLColor color) {
    if (!gfx || !text || !text[0]) return;
    
    TTF_Font* font = gfx->font;
    if (size >= 16) font = gfx->font_large;
    else if (size <= 12) font = gfx->font_small;
    
    if (!font) {
        /* Fallback: draw rectangles */
        SDL_SetRenderDrawColor(gfx->renderer, color.r, color.g, color.b, color.a);
        float char_w = size * 0.6f;
        float xx = x;
        for (const char* c = text; *c; c++) {
            if (*c != ' ') {
                SDL_FRect r = {xx, y, char_w - 1, size};
                SDL_RenderFillRectF(gfx->renderer, &r);
            }
            xx += char_w;
        }
        return;
    }
    
    SDL_Color sdl_color = {color.r, color.g, color.b, color.a};
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, sdl_color);
    if (!surface) return;
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(gfx->renderer, surface);
    if (texture) {
        SDL_FRect dst = {x, y, (float)surface->w, (float)surface->h};
        SDL_RenderCopyF(gfx->renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void rl_graphics_measure_text(RLGraphics* gfx, const char* text, float size, float* width, float* height) {
    if (!gfx || !text) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    
    TTF_Font* font = gfx->font;
    if (size >= 16) font = gfx->font_large;
    else if (size <= 12) font = gfx->font_small;
    
    if (!font) {
        if (width) *width = strlen(text) * size * 0.6f;
        if (height) *height = size;
        return;
    }
    
    int w, h;
    TTF_SizeUTF8(font, text, &w, &h);
    if (width) *width = (float)w;
    if (height) *height = (float)h;
}

void rl_graphics_get_mouse(RLGraphics* gfx, float* x, float* y) {
    if (!gfx) return;
    if (x) *x = gfx->mouse_x;
    if (y) *y = gfx->mouse_y;
}

bool rl_graphics_is_mouse_down(RLGraphics* gfx, int button) {
    if (!gfx) return false;
    uint32_t mask = 0;
    switch (button) {
        case 0: mask = SDL_BUTTON_LMASK; break;
        case 1: mask = SDL_BUTTON_MMASK; break;
        case 2: mask = SDL_BUTTON_RMASK; break;
    }
    return (gfx->mouse_state & mask) != 0;
}

bool rl_graphics_is_key_down(RLGraphics* gfx, int key) {
    (void)gfx;
    const uint8_t* state = SDL_GetKeyboardState(NULL);
    return state[SDL_GetScancodeFromKey(key)] != 0;
}

const char* rl_graphics_get_text_input(RLGraphics* gfx) {
    return gfx ? gfx->text_input : "";
}
