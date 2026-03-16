#ifndef NX_MINIMAL_H
#define NX_MINIMAL_H

/* Include order matters to avoid conflicts */
#include "nxgfx/nxgfx.h"
#include "nxgfx/nxgfx_effects.h" 
/* Define conflicts guards */
#define NX_BLEND_MODE_DEFINED
#define NX_FRAME_STATS_DEFINED

#include "nxrender/window.h"
#include "nxrender/application.h"

#endif
