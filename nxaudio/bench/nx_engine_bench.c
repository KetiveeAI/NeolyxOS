/* Copyright (c) 2025 Swanaya Gupta
 * KETIVEEAI License v1.1 - Always Free.
 * See LICENSE in repo root.
 */

/*
 * NXAudio Performance Benchmark
 * 
 * Measure spatial audio rendering performance:
 * - Object capacity at target CPU budget
 * - Latency measurement
 * - Throughput analysis
 * 
 * Usage: ./nx_engine_bench --objects 32 --frames 1024 --duration 60
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "../spatial/nx_spatial.h"

#define SAMPLE_RATE     48000
#define BLOCK_SIZE      1024

/* Get time in microseconds */
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* Benchmark result */
typedef struct {
    int num_objects;
    int block_size;
    double avg_render_us;
    double max_render_us;
    double cpu_percent;
    double objects_per_ms;
} bench_result_t;

/* Run benchmark */
static void run_benchmark(int num_objects, int block_size, int duration_sec,
                           bench_result_t *result) {
    printf("Benchmark: %d objects, %d frames, %d sec\n",
           num_objects, block_size, duration_sec);
    
    /* Create spatial engine */
    nx_spatial_t *sp = nx_spatial_create(SAMPLE_RATE, block_size);
    if (!sp) {
        fprintf(stderr, "Failed to create spatial engine\n");
        return;
    }
    
    nx_spatial_load_hrtf_default(sp);
    
    /* Add objects */
    for (int i = 0; i < num_objects; i++) {
        float angle = (float)i * 2.0f * 3.14159f / num_objects;
        
        nx_object_t obj = {
            .id = i,
            .active = 1,
            .position = {3.0f * sinf(angle), 0, 3.0f * cosf(angle)},
            .velocity = {0, 0, 0},
            .gain = 1.0f,
            .min_distance = 1.0f,
            .max_distance = 20.0f,
            .rolloff = 1.0f,
            .spatial_enabled = 1
        };
        
        nx_spatial_add_object(sp, i, &obj);
    }
    
    /* Allocate buffers */
    float **mono_buffers = (float**)malloc(num_objects * sizeof(float*));
    for (int i = 0; i < num_objects; i++) {
        mono_buffers[i] = (float*)calloc(block_size, sizeof(float));
        /* Fill with test data */
        for (int j = 0; j < block_size; j++) {
            mono_buffers[i][j] = 0.1f * sinf(j * 0.1f + i);
        }
    }
    
    float *stereo_out = (float*)malloc(block_size * 2 * sizeof(float));
    
    /* Timing */
    int total_blocks = (SAMPLE_RATE * duration_sec) / block_size;
    uint64_t total_time = 0;
    uint64_t max_time = 0;
    
    printf("Running %d blocks...\n", total_blocks);
    
    for (int b = 0; b < total_blocks; b++) {
        /* Update positions */
        float t = (float)b * block_size / SAMPLE_RATE;
        for (int i = 0; i < num_objects; i++) {
            float angle = (float)i * 2.0f * 3.14159f / num_objects + t * 0.5f;
            nx_spatial_set_object_pos(sp, i,
                                       3.0f * sinf(angle), 0, 3.0f * cosf(angle));
        }
        
        /* Time render */
        uint64_t start = get_time_us();
        nx_spatial_render(sp, mono_buffers, block_size, stereo_out);
        uint64_t elapsed = get_time_us() - start;
        
        total_time += elapsed;
        if (elapsed > max_time) max_time = elapsed;
        
        /* Progress */
        if (b % 100 == 0) {
            printf("\r  Block %d/%d", b, total_blocks);
            fflush(stdout);
        }
    }
    
    printf("\r                       \n");
    
    /* Calculate results */
    result->num_objects = num_objects;
    result->block_size = block_size;
    result->avg_render_us = (double)total_time / total_blocks;
    result->max_render_us = (double)max_time;
    
    /* CPU budget: block_size frames at SAMPLE_RATE */
    double block_duration_us = (double)block_size / SAMPLE_RATE * 1000000.0;
    result->cpu_percent = (result->avg_render_us / block_duration_us) * 100.0;
    
    result->objects_per_ms = (double)num_objects / (result->avg_render_us / 1000.0);
    
    /* Cleanup */
    for (int i = 0; i < num_objects; i++) {
        free(mono_buffers[i]);
    }
    free(mono_buffers);
    free(stereo_out);
    nx_spatial_destroy(sp);
}

int main(int argc, char *argv[]) {
    printf("=================================\n");
    printf("   NXAudio Performance Benchmark\n");
    printf("=================================\n\n");
    
    int num_objects = 32;
    int block_size = BLOCK_SIZE;
    int duration = 10;
    
    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--objects") == 0 && i + 1 < argc) {
            num_objects = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            block_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("  --objects N    Number of spatial objects (default: 32)\n");
            printf("  --frames N     Block size in frames (default: 1024)\n");
            printf("  --duration N   Test duration in seconds (default: 10)\n");
            return 0;
        }
    }
    
    printf("Configuration:\n");
    printf("  Sample rate: %d Hz\n", SAMPLE_RATE);
    printf("  Block size:  %d frames\n", block_size);
    printf("  Objects:     %d\n", num_objects);
    printf("  Duration:    %d sec\n\n", duration);
    
    bench_result_t result;
    run_benchmark(num_objects, block_size, duration, &result);
    
    printf("\n--- Results ---\n");
    printf("  Average render time: %.2f us\n", result.avg_render_us);
    printf("  Maximum render time: %.2f us\n", result.max_render_us);
    printf("  CPU usage:           %.2f%%\n", result.cpu_percent);
    printf("  Objects per ms:      %.1f\n", result.objects_per_ms);
    
    if (result.cpu_percent < 50.0) {
        printf("\n  Status: PASS - Under 50%% CPU budget\n");
    } else if (result.cpu_percent < 100.0) {
        printf("\n  Status: WARN - Above 50%% CPU budget\n");
    } else {
        printf("\n  Status: FAIL - Exceeds real-time budget\n");
    }
    
    /* Run scaling test */
    printf("\n--- Scaling Test ---\n");
    int test_counts[] = {8, 16, 32, 64};
    
    for (size_t i = 0; i < sizeof(test_counts)/sizeof(test_counts[0]); i++) {
        bench_result_t r;
        run_benchmark(test_counts[i], block_size, 5, &r);
        printf("  %2d objects: %.2f us avg, %.1f%% CPU\n",
               r.num_objects, r.avg_render_us, r.cpu_percent);
    }
    
    printf("\nBenchmark complete.\n");
    
    return 0;
}
