/*
 * NXVideo Core Implementation
 * 
 * Provides initialization, hardware detection, and system management.
 */

#include "nxvideo/nxvideo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <dlfcn.h>
#endif

/* ============ Internal State ============ */
static int g_initialized = 0;
static int g_hw_count = 0;

typedef struct {
    nxvideo_hw_caps_t caps;
    void *handle;       /* dlopen handle */
} hw_decoder_t;

static hw_decoder_t g_hw_decoders[8];

/* ============ Error Strings ============ */
static const char* g_error_strings[] = {
    "Success",
    "Initialization error",
    "Invalid parameter",
    "Out of memory",
    "No device available",
    "I/O error",
    "Format error",
    "Decode error",
    "End of file",
    "Unsupported format",
};

/* ===========================================================================
 * Hardware Detection - Linux VAAPI
 * =========================================================================*/
#ifdef __linux__
static int detect_vaapi(void) {
    void *handle = dlopen("libva.so.2", RTLD_LAZY);
    if (!handle) {
        handle = dlopen("libva.so", RTLD_LAZY);
    }
    if (!handle) return 0;
    
    /* Check for VAAPI functions */
    void *init_func = dlsym(handle, "vaInitialize");
    if (init_func) {
        hw_decoder_t *hw = &g_hw_decoders[g_hw_count];
        hw->handle = handle;
        hw->caps.type = NXVIDEO_HW_VAAPI;
        strncpy(hw->caps.name, "VA-API (Intel/AMD)", 63);
        hw->caps.supports_h264 = 1;
        hw->caps.supports_h265 = 1;
        hw->caps.supports_vp9 = 1;
        hw->caps.supports_av1 = 0;  /* Depends on driver */
        hw->caps.supports_10bit = 1;
        hw->caps.max_width = 8192;
        hw->caps.max_height = 4320;
        g_hw_count++;
        return 1;
    }
    
    dlclose(handle);
    return 0;
}

static int detect_nvdec(void) {
    void *handle = dlopen("libnvcuvid.so.1", RTLD_LAZY);
    if (!handle) {
        handle = dlopen("libnvcuvid.so", RTLD_LAZY);
    }
    if (!handle) return 0;
    
    hw_decoder_t *hw = &g_hw_decoders[g_hw_count];
    hw->handle = handle;
    hw->caps.type = NXVIDEO_HW_NVDEC;
    strncpy(hw->caps.name, "NVDEC (NVIDIA)", 63);
    hw->caps.supports_h264 = 1;
    hw->caps.supports_h265 = 1;
    hw->caps.supports_vp9 = 1;
    hw->caps.supports_av1 = 1;
    hw->caps.supports_10bit = 1;
    hw->caps.max_width = 8192;
    hw->caps.max_height = 8192;
    g_hw_count++;
    return 1;
}

static int detect_vdpau(void) {
    void *handle = dlopen("libvdpau.so.1", RTLD_LAZY);
    if (!handle) return 0;
    
    hw_decoder_t *hw = &g_hw_decoders[g_hw_count];
    hw->handle = handle;
    hw->caps.type = NXVIDEO_HW_VDPAU;
    strncpy(hw->caps.name, "VDPAU (NVIDIA Legacy)", 63);
    hw->caps.supports_h264 = 1;
    hw->caps.supports_h265 = 0;
    hw->caps.supports_vp9 = 0;
    hw->caps.supports_av1 = 0;
    hw->caps.supports_10bit = 0;
    hw->caps.max_width = 4096;
    hw->caps.max_height = 4096;
    g_hw_count++;
    return 1;
}
#endif

#ifdef __APPLE__
static int detect_videotoolbox(void) {
    /* VideoToolbox always available on macOS */
    hw_decoder_t *hw = &g_hw_decoders[g_hw_count];
    hw->caps.type = NXVIDEO_HW_VIDEOTOOLBOX;
    strncpy(hw->caps.name, "VideoToolbox (Apple)", 63);
    hw->caps.supports_h264 = 1;
    hw->caps.supports_h265 = 1;
    hw->caps.supports_vp9 = 0;
    hw->caps.supports_av1 = 0;
    hw->caps.supports_10bit = 1;
    hw->caps.max_width = 8192;
    hw->caps.max_height = 8192;
    g_hw_count++;
    return 1;
}
#endif

#ifdef _WIN32
static int detect_d3d11va(void) {
    HMODULE h = LoadLibraryA("d3d11.dll");
    if (!h) return 0;
    
    hw_decoder_t *hw = &g_hw_decoders[g_hw_count];
    hw->caps.type = NXVIDEO_HW_D3D11VA;
    strncpy(hw->caps.name, "D3D11VA (Windows)", 63);
    hw->caps.supports_h264 = 1;
    hw->caps.supports_h265 = 1;
    hw->caps.supports_vp9 = 1;
    hw->caps.supports_av1 = 0;
    hw->caps.supports_10bit = 1;
    hw->caps.max_width = 8192;
    hw->caps.max_height = 8192;
    g_hw_count++;
    return 1;
}
#endif

/* ===========================================================================
 * System API
 * =========================================================================*/

nxvideo_error_t nxvideo_init(void) {
    if (g_initialized) {
        return NXVIDEO_SUCCESS;
    }
    
    printf("[NXVideo] Initializing v%s\n", NXVIDEO_VERSION_STRING);
    
    /* Detect hardware decoders */
    g_hw_count = 0;
    memset(g_hw_decoders, 0, sizeof(g_hw_decoders));
    
#ifdef __linux__
    detect_vaapi();
    detect_nvdec();
    detect_vdpau();
#endif

#ifdef __APPLE__
    detect_videotoolbox();
#endif

#ifdef _WIN32
    detect_d3d11va();
#endif
    
    printf("[NXVideo] Found %d hardware decoder(s)\n", g_hw_count);
    for (int i = 0; i < g_hw_count; i++) {
        printf("[NXVideo]   %d: %s\n", i, g_hw_decoders[i].caps.name);
    }
    
    g_initialized = 1;
    return NXVIDEO_SUCCESS;
}

void nxvideo_shutdown(void) {
    if (!g_initialized) return;
    
    printf("[NXVideo] Shutting down\n");
    
    /* Close hardware decoder handles */
#ifdef __linux__
    for (int i = 0; i < g_hw_count; i++) {
        if (g_hw_decoders[i].handle) {
            dlclose(g_hw_decoders[i].handle);
        }
    }
#endif
    
    g_hw_count = 0;
    g_initialized = 0;
}

const char* nxvideo_version(void) {
    return NXVIDEO_VERSION_STRING;
}

const char* nxvideo_error_string(nxvideo_error_t error) {
    int idx = -error;
    if (idx < 0 || idx >= (int)(sizeof(g_error_strings) / sizeof(g_error_strings[0]))) {
        return "Unknown error";
    }
    return g_error_strings[idx];
}

/* ===========================================================================
 * Hardware Detection API
 * =========================================================================*/

int nxvideo_hw_count(void) {
    return g_hw_count;
}

nxvideo_error_t nxvideo_hw_caps(int index, nxvideo_hw_caps_t *caps) {
    if (index < 0 || index >= g_hw_count) {
        return NXVIDEO_ERROR_INVALID;
    }
    if (!caps) {
        return NXVIDEO_ERROR_INVALID;
    }
    
    *caps = g_hw_decoders[index].caps;
    return NXVIDEO_SUCCESS;
}

nxvideo_hw_type_t nxvideo_hw_best(nxvideo_codec_t codec) {
    /* Priority: NVDEC > VAAPI > VideoToolbox > D3D11VA > VDPAU */
    nxvideo_hw_type_t priority[] = {
        NXVIDEO_HW_NVDEC,
        NXVIDEO_HW_VAAPI,
        NXVIDEO_HW_VIDEOTOOLBOX,
        NXVIDEO_HW_D3D11VA,
        NXVIDEO_HW_VDPAU,
    };
    
    for (int p = 0; p < 5; p++) {
        for (int i = 0; i < g_hw_count; i++) {
            if (g_hw_decoders[i].caps.type != priority[p]) continue;
            
            nxvideo_hw_caps_t *caps = &g_hw_decoders[i].caps;
            int supported = 0;
            
            switch (codec) {
                case NXVIDEO_CODEC_H264: supported = caps->supports_h264; break;
                case NXVIDEO_CODEC_H265: supported = caps->supports_h265; break;
                case NXVIDEO_CODEC_VP9:  supported = caps->supports_vp9;  break;
                case NXVIDEO_CODEC_AV1:  supported = caps->supports_av1;  break;
                default: break;
            }
            
            if (supported) {
                return priority[p];
            }
        }
    }
    
    return NXVIDEO_HW_NONE;
}

int nxvideo_hw_available(nxvideo_codec_t codec) {
    return nxvideo_hw_best(codec) != NXVIDEO_HW_NONE;
}
