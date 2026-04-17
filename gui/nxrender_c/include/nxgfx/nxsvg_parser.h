/*
 * NeolyxOS - NXSVG DOM Parser
 * Copyright (c) 2025 KetiveeAI
 * 
 * Implements a pure C XML-like DOM tree specialized for SVG layout parsing,
 * including structural inheritance (<g> tags) and SVG presentation attributes.
 */

#ifndef NXSVG_PARSER_H
#define NXSVG_PARSER_H

#include "nxgfx_path.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NXSVG_NODE_SVG,
    NXSVG_NODE_G,
    NXSVG_NODE_PATH,
    NXSVG_NODE_DEFS,
    NXSVG_NODE_LINEAR_GRADIENT,
    NXSVG_NODE_RADIAL_GRADIENT,
    NXSVG_NODE_STOP,
    NXSVG_NODE_UNKNOWN
} nxsvg_node_type_t;

/* Structural XML-like Node Map */
typedef struct nxsvg_node {
    nxsvg_node_type_t type;
    char id[64];
    
    /* Inherited state */
    bool has_transform;
    float transform[6];
    nx_color_t fill_color;
    char fill_ref[64];         /* If filled via gradient URL */
    nx_color_t stroke_color;
    float stroke_width;
    float opacity;
    
    /* Path specific data */
    char* path_data_d;
    
    /* Document Tree Hierarchy */
    struct nxsvg_node* parent;
    struct nxsvg_node* child;  /* First child */
    struct nxsvg_node* next;   /* Next sibling */
} nxsvg_node_t;

typedef struct {
    nxsvg_node_t* root_node;
    float width;
    float height;
    float viewbox[4];          /* min-x, min-y, width, height */
} nxsvg_dom_t;

/*
 * Parses a buffer containing pure SVG XML.
 * Constructs a DOM tree resolving <g> tag inheritance.
 */
nxsvg_dom_t* nxsvg_parse_dom(const char* xml_data, uint32_t length);

/* Frees the DOM tree and all child nodes recursively. */
void nxsvg_free_dom(nxsvg_dom_t* dom);

#ifdef __cplusplus
}
#endif
#endif
