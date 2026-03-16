/*
 * SVG Import for IconLay
 * 
 * Parse SVG files and convert to NXI layers.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef SVG_IMPORT_H
#define SVG_IMPORT_H

#include "nxi_format.h"
#include <stdint.h>

/* Import options */
typedef struct {
    uint32_t default_color;  /* Default fill color if none specified */
    bool flatten_groups;     /* Flatten SVG groups into single layer */
    bool preserve_transform; /* Apply SVG transforms to paths */
} svg_import_opts_t;

/* Default import options */
svg_import_opts_t svg_import_defaults(void);

/* Import SVG file as a layer
 * Returns NULL on failure
 */
nxi_layer_t* svg_import_layer(const char* svg_path, 
                               const svg_import_opts_t* opts);

/* Import SVG from memory */
nxi_layer_t* svg_import_layer_mem(const char* svg_data, uint32_t len,
                                   const svg_import_opts_t* opts);

/* Import SVG as multiple layers (one per path element) */
int svg_import_multilayer(const char* svg_path,
                          nxi_icon_t* icon,
                          const svg_import_opts_t* opts);

/* Convert SVG path 'd' attribute to NXI path commands */
int svg_parse_path_d(const char* d, 
                     nxi_path_cmd_t** out_cmds, 
                     uint32_t* out_count);

/* Get natural size of SVG (from viewBox or width/height) */
void svg_get_dimensions(const char* svg_data, 
                        float* out_width, float* out_height);

#endif /* SVG_IMPORT_H */
