/*
 * NXCamera Kernel Driver Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxcam_kdrv.h"
#include <stddef.h>

extern void serial_puts(const char *s);
extern void serial_putc(char c);

static struct {
    int initialized;
    nxcam_info_t devices[NXCAM_MAX_DEVICES];
    int device_count;
} g_cam = {0};

static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

int nxcam_kdrv_init(void) {
    if (g_cam.initialized) return 0;
    
    serial_puts("[NXCamera] Initializing v" NXCAM_KDRV_VERSION "\n");
    
    g_cam.device_count = 0;
    
    /* TODO: Probe USB for UVC cameras */
    
    g_cam.initialized = 1;
    serial_puts("[NXCamera] ");
    serial_dec(g_cam.device_count);
    serial_puts(" camera(s) found\n");
    return 0;
}

void nxcam_kdrv_shutdown(void) {
    for (int i = 0; i < g_cam.device_count; i++) {
        if (g_cam.devices[i].streaming) {
            nxcam_kdrv_stop_stream(i);
        }
    }
    g_cam.initialized = 0;
}

int nxcam_kdrv_count(void) {
    return g_cam.device_count;
}

int nxcam_kdrv_get_info(int id, nxcam_info_t *info) {
    if (!g_cam.initialized || id < 0 || id >= g_cam.device_count || !info) return -1;
    *info = g_cam.devices[id];
    return 0;
}

int nxcam_kdrv_start_stream(int id, uint16_t width, uint16_t height) {
    if (!g_cam.initialized || id < 0 || id >= g_cam.device_count) return -1;
    
    g_cam.devices[id].width = width;
    g_cam.devices[id].height = height;
    g_cam.devices[id].streaming = 1;
    
    serial_puts("[NXCamera] Stream started: ");
    serial_dec(width);
    serial_putc('x');
    serial_dec(height);
    serial_puts("\n");
    return 0;
}

int nxcam_kdrv_stop_stream(int id) {
    if (!g_cam.initialized || id < 0 || id >= g_cam.device_count) return -1;
    g_cam.devices[id].streaming = 0;
    serial_puts("[NXCamera] Stream stopped\n");
    return 0;
}

int nxcam_kdrv_get_frame(int id, nxcam_frame_t *frame) {
    if (!g_cam.initialized || id < 0 || id >= g_cam.device_count || !frame) return -1;
    if (!g_cam.devices[id].streaming) return -2;
    
    /* TODO: Get frame from camera */
    frame->data = NULL;
    frame->size = 0;
    return -1;
}

void nxcam_kdrv_debug(void) {
    serial_puts("\n=== NXCamera Debug ===\n");
    serial_puts("Cameras: ");
    serial_dec(g_cam.device_count);
    serial_puts("\n======================\n\n");
}
