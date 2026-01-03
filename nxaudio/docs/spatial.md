# NeolyxSpatial - 3D Audio API

## Overview

NeolyxSpatial provides binaural 3D audio rendering for NeolyxOS, 
replacing third-party solutions like Dolby Atmos.

## Features

- HRTF-based binaural rendering
- Time-domain FIR convolution
- Distance attenuation (inverse/linear/exponential)
- Doppler effect simulation
- 64 simultaneous spatial objects
- Height virtualization

## Quick Start

```c
#include "spatial/nx_spatial.h"

/* Create engine */
nx_spatial_t *sp = nx_spatial_create(48000, 1024);
nx_spatial_load_hrtf_default(sp);

/* Set listener */
nx_spatial_set_listener_pos(sp, 0, 0, 0);
nx_spatial_set_listener_orientation(sp, 0, 0, -1, 0, 1, 0);

/* Add sound object */
nx_object_t obj = {
    .position = {3.0f, 0.0f, -2.0f},
    .gain = 1.0f,
    .min_distance = 1.0f,
    .max_distance = 20.0f
};
nx_spatial_add_object(sp, 1, &obj);

/* Render */
nx_spatial_render_object(sp, 1, mono_input, frames, stereo_output);

/* Cleanup */
nx_spatial_destroy(sp);
```

## HRTF Data

Default synthetic HRTF is included. For higher quality:

1. Place HRTF files in `data/hrtf/`
2. SOFA format support planned
3. Custom binary format: `[rate][len][elev][azim][filters_L][filters_R]`

## Performance

At 48kHz, 1024 frame blocks:

| Objects | CPU Usage |
|---------|-----------|
| 8       | < 5%      |
| 32      | < 20%     |
| 64      | < 40%     |

Target: 64 objects at < 50% CPU budget.

## Integration with NXAudio Engine

```c
/* Attach spatial to engine */
nx_spatial_t *sp = nx_spatial_create(48000, 1024);
nx_spatial_load_hrtf_default(sp);
nx_engine_set_spatial(&engine, sp);

/* Enable per-sound */
nx_engine_set_sound_spatial(&engine, sound_id, 1);
nx_engine_set_sound_position(&engine, sound_id, x, y, z);
```

## Files

| File | Purpose |
|------|---------|
| nx_spatial.h | Public API |
| nx_spatial.c | HRTF/FIR implementation |
| spatial_test.c | Test harness |
| nx_engine_bench.c | Performance benchmark |
