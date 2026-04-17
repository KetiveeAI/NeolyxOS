/*
 * NeolyxOS - NXSVG DOM Parser Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/nxsvg_parser.h"
#include <string.h>

/* Use external malloc/free assuming OS bindings */
extern void* malloc(size_t size);
extern void* calloc(size_t n, size_t size);
extern void free(void* ptr);

/* Lexical string helpers */
static const char* skip_ws(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static bool match_str(const char* p, const char* str) {
    while (*str) {
        if (*p != *str) return false;
        p++; str++;
    }
    return true;
}

static nxsvg_node_t* create_node(nxsvg_node_type_t type) {
    nxsvg_node_t* node = (nxsvg_node_t*)calloc(1, sizeof(nxsvg_node_t));
    if (node) {
        node->type = type;
        node->fill_color = nx_rgba(255, 255, 255, 0); /* Transparent default */
        node->stroke_color = nx_rgba(255, 255, 255, 0);
        node->opacity = 1.0f;
        node->transform[0] = 1; node->transform[3] = 1; /* Identity scale */
    }
    return node;
}

void nxsvg_free_dom_node(nxsvg_node_t* node) {
    if (!node) return;
    
    nxsvg_free_dom_node(node->child);
    nxsvg_free_dom_node(node->next);
    
    if (node->path_data_d) free(node->path_data_d);
    free(node);
}

void nxsvg_free_dom(nxsvg_dom_t* dom) {
    if (!dom) return;
    nxsvg_free_dom_node(dom->root_node);
    free(dom);
}

/* Extract attribute strictly within tag bounds */
static bool get_attr(const char* tag_start, const char* tag_end, const char* attr_name, char* out_buf, uint32_t out_len) {
    uint32_t name_len = 0;
    while (attr_name[name_len]) name_len++;
    
    const char* p = tag_start;
    while (p < tag_end) {
        if (p[0] == attr_name[0]) {
            bool matches = true;
            for (uint32_t i = 0; i < name_len; i++) {
                if (p + i >= tag_end || p[i] != attr_name[i]) {
                    matches = false; break;
                }
            }
            if (matches && p + name_len < tag_end && p[name_len] == '=') {
                const char* val_start = p + name_len + 1;
                while (val_start < tag_end && (*val_start == ' ' || *val_start == '\t')) val_start++;
                if (val_start < tag_end && (*val_start == '"' || *val_start == '\'')) {
                    char quote = *val_start;
                    val_start++;
                    const char* val_end = val_start;
                    while (val_end < tag_end && *val_end != quote) val_end++;
                    
                    uint32_t len = val_end - val_start;
                    if (len >= out_len) len = out_len - 1;
                    
                    for (uint32_t i = 0; i < len; i++) out_buf[i] = val_start[i];
                    out_buf[len] = '\0';
                    return true;
                }
            }
        }
        p++;
    }
    return false;
}

static nxsvg_node_t* parse_node_recursive(const char** p_xml, const char* end) {
    const char* p = *p_xml;
    p = skip_ws(p);
    
    if (p >= end || *p != '<') {
        *p_xml = end; 
        return NULL;
    }
    
    p++; /* skip '<' */
    
    /* Handle comments and closures */
    if (*p == '!' || *p == '?') {
        while (p < end && *p != '>') p++;
        if (p < end) p++;
        *p_xml = p;
        return parse_node_recursive(p_xml, end);
    }
    if (*p == '/') {
        /* Closing tag */
        while (p < end && *p != '>') p++;
        if (p < end) p++;
        *p_xml = p;
        return NULL;
    }
    
    /* Identifier parsing */
    const char* tag_name_start = p;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '>' && *p != '/') p++;
    uint32_t tag_len = p - tag_name_start;
    
    /* Find end of tag attributes */
    const char* attr_start = p;
    while (p < end && *p != '>' && *p != '/') p++;
    const char* attr_end = p;
    
    bool self_closing = false;
    if (p < end && *p == '/') {
        self_closing = true;
        p++;
    }
    if (p < end && *p == '>') p++;
    
    nxsvg_node_type_t type = NXSVG_NODE_UNKNOWN;
    if (tag_len == 3 && match_str(tag_name_start, "svg")) type = NXSVG_NODE_SVG;
    else if (tag_len == 1 && *tag_name_start == 'g') type = NXSVG_NODE_G;
    else if (tag_len == 4 && match_str(tag_name_start, "path")) type = NXSVG_NODE_PATH;
    else if (tag_len == 4 && match_str(tag_name_start, "defs")) type = NXSVG_NODE_DEFS;
    
    nxsvg_node_t* node = create_node(type);
    if (!node) return NULL;
    
    /* Extract standard attributes */
    char attr_buf[1024];
    if (get_attr(attr_start, attr_end, "id", attr_buf, sizeof(attr_buf))) {
        for (uint32_t i = 0; i < 63 && attr_buf[i]; i++) node->id[i] = attr_buf[i];
    }
    
    if (type == NXSVG_NODE_PATH) {
        if (get_attr(attr_start, attr_end, "d", attr_buf, sizeof(attr_buf))) {
            uint32_t d_len = 0; while (attr_buf[d_len]) d_len++;
            node->path_data_d = (char*)malloc(d_len + 1);
            if (node->path_data_d) {
                for (uint32_t i = 0; i <= d_len; i++) node->path_data_d[i] = attr_buf[i];
            }
        }
    }
    
    *p_xml = p;
    
    /* Parse children if not self-closing */
    if (!self_closing) {
        nxsvg_node_t* last_child = NULL;
        while (1) {
            const char* next_p = *p_xml;
            nxsvg_node_t* child = parse_node_recursive(&next_p, end);
            *p_xml = next_p;
            
            if (!child) {
                /* Was either EOF or closing tag */
                break;
            }
            
            child->parent = node;
            if (!node->child) {
                node->child = child;
            } else {
                last_child->next = child;
            }
            last_child = child;
        }
    }
    
    return node;
}

nxsvg_dom_t* nxsvg_parse_dom(const char* xml_data, uint32_t length) {
    if (!xml_data || length == 0) return NULL;
    
    nxsvg_dom_t* dom = (nxsvg_dom_t*)calloc(1, sizeof(nxsvg_dom_t));
    if (!dom) return NULL;
    
    const char* ptr = xml_data;
    const char* end = xml_data + length;
    
    nxsvg_node_t* root = NULL;
    while (ptr < end && !root) {
        root = parse_node_recursive(&ptr, end);
        if (root && root->type != NXSVG_NODE_SVG) {
            nxsvg_free_dom_node(root);
            root = NULL;
        }
    }
    
    dom->root_node = root;
    return dom;
}
