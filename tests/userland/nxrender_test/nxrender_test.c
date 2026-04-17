/*
 * NeolyxOS Userspace NXRender Math & Geometry Tests
 * 
 * Verifies core structures required by the vector engine (nxgfx_path)
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/syscall.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* We need nxgfx_path and nxsvg_parser definitions */
#include "nxgfx/nxgfx_path.h"
#include "nxgfx/nxsvg_parser.h"

/* Minimal bump allocator for freestanding environment */
static uint8_t heap[1024 * 1024 * 4]; /* 4MB heap for testing */
static size_t heap_offset = 0;

void* malloc(size_t size) {
    if (heap_offset + size > sizeof(heap)) return NULL;
    void* ptr = &heap[heap_offset];
    heap_offset += size;
    return ptr;
}

void* calloc(size_t n, size_t size) {
    size_t total = n * size;
    void* p = malloc(total);
    if (p) {
        uint8_t* b = (uint8_t*)p;
        for (size_t i = 0; i < total; i++) b[i] = 0;
    }
    return p;
}

void free(void* ptr) {
    (void)ptr; /* Bump allocator cannot free individual blocks */
}

/* Redefine internal math for nx_bezier since <math.h> is unavailable in tests */
float sqrtf(float x) {
    /* Fast inverse square root hack for basic testing */
    if (x <= 0) return 0;
    int i = *(int*)&x;
    i = 0x5f3759df - (i >> 1);
    float y = *(float*)&i;
    y = y * (1.5f - (x * 0.5f * y * y));
    y = y * (1.5f - (x * 0.5f * y * y));
    return x * y;
}

float fabsf(float x) {
    return x < 0 ? -x : x;
}

float sinf(float x) {
    /* Very crude Taylor series for testing: x - x^3/6 + x^5/120 */
    float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - 0.00833333f * x2));
}

float cosf(float x) {
    /* Very crude Taylor series for testing: 1 - x^2/2 + x^4/24 */
    float x2 = x * x;
    return 1.0f - x2 * (0.5f - 0.04166667f * x2);
}

void* memcpy(void* dest, const void* src, size_t n) {
    char* d = dest;
    const char* s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        print("[FAIL] "); \
        println(msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    print("[PASS] "); \
    println(msg); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;
static int tests_failed = 0;

static void print_num(int n) {
    char buf[12];
    int i = 0;
    if (n == 0) { print("0"); return; }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        char c = buf[--i];
        write(STDOUT_FD, &c, 1);
    }
}

/* ======================================================== */

static void test_bezier_subdivision(void) {
    println("\n--- Bezier Subdivision Tests ---");
    
    nx_vec2_t points[100];
    uint32_t count = 0;
    
    nx_vec2_t p0 = {0.0f, 0.0f};
    nx_vec2_t p1 = {50.0f, 100.0f};
    nx_vec2_t p2 = {100.0f, 0.0f};
    
    nx_bezier_flatten_quad(p0, p1, p2, 0.5f, points, &count, 100);
    
    /* A quad with substantial curvature should produce multiple segments */
    TEST_ASSERT(count >= 3 && count <= 100, "Math: Quad Bezier subdivides correctly");
    
    TEST_PASS("Quad Bezier tests passed");
}

static void test_svg_parser(void) {
    println("\n--- SVG Parsing Tests ---");
    
    const char* svg_doc = 
        "<svg width=\"100\" height=\"100\">"
        "<g id=\"group1\" transform=\"translate(10)\">"
        "<path d=\"M0,0 L10,10 Z\" fill=\"#ff0000\"/>"
        "</g>"
        "</svg>";
        
    nxsvg_dom_t* dom = nxsvg_parse_dom(svg_doc, 114); /* Approx length */
    
    TEST_ASSERT(dom != NULL, "XML: DOM parser initializes");
    if (dom && dom->root_node) {
        TEST_ASSERT(dom->root_node->type == NXSVG_NODE_SVG, "XML: Root node is <svg>");
        
        nxsvg_node_t* g_node = dom->root_node->child;
        TEST_ASSERT(g_node && g_node->type == NXSVG_NODE_G, "XML: First child is <g>");
        
        nxsvg_node_t* path_node = g_node->child;
        TEST_ASSERT(path_node && path_node->type == NXSVG_NODE_PATH, "XML: Leaf node is <path>");
    }
    
    TEST_PASS("SVG parser tests passed");
}

void _start(void) {
    println("════════════════════════════════════════");
    println("  NXRender C Userspace Test Suite");
    println("════════════════════════════════════════");
    
    test_bezier_subdivision();
    test_svg_parser();
    
    println("\n════════════════════════════════════════");
    print("  Passed: "); print_num(tests_passed); println("");
    print("  Failed: "); print_num(tests_failed); println("");
    
    if (tests_failed == 0) {
        println("✓ ALL NXRENDER TESTS PASSED!");
    } else {
        println("✗ SOME NXRENDER TESTS FAILED.");
    }
    
    exit(tests_failed ? 1 : 0);
}
