/*
 * Reolab - UI Renderer Header
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef REOLAB_UI_H
#define REOLAB_UI_H

#include "../graphics/graphics.h"
#include "../app/app.h"

/* Main render function */
void rl_ui_render(RLGraphics* gfx, ReoLabApp* app);

/* Component renderers */
void rl_ui_render_sidebar(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h);
void rl_ui_render_toolbar(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h);
void rl_ui_render_tabs(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h);
void rl_ui_render_editor(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h);
void rl_ui_render_statusbar(RLGraphics* gfx, ReoLabApp* app, float x, float y, float w, float h);

#endif /* REOLAB_UI_H */
