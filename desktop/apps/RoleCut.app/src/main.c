/*
 * RoleCut Video Editor - Main Entry Point
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#include "rolecut.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Application State
 * ============================================================================ */

typedef struct rolecut_app {
    rc_project_t *project;
    bool running;
    bool modified;
    
    /* UI state */
    int selected_track;
    rc_clip_t *selected_clip;
    float zoom_level;
    rc_time_t scroll_position;
    
    /* Playback */
    rc_playback_state_t playback_state;
} rolecut_app_t;

static rolecut_app_t g_app = {0};

/* ============================================================================
 * Application Lifecycle
 * ============================================================================ */

static bool app_init(void) {
    printf("RoleCut %s starting...\n", ROLECUT_VERSION_STRING);
    
    g_app.running = true;
    g_app.zoom_level = 1.0f;
    g_app.selected_track = -1;
    
    return true;
}

static void app_shutdown(void) {
    if (g_app.project) {
        rc_project_destroy(g_app.project);
        g_app.project = NULL;
    }
    
    printf("RoleCut shutdown complete\n");
}

/* ============================================================================
 * Project Management
 * ============================================================================ */

static bool app_new_project(const char *name, rc_resolution_t res, double fps) {
    if (g_app.project) {
        rc_project_destroy(g_app.project);
    }
    
    rc_project_settings_t settings = {
        .resolution = res,
        .fps = fps,
        .sample_rate = 48000,
    };
    strncpy(settings.name, name, sizeof(settings.name) - 1);
    
    g_app.project = rc_project_create(&settings);
    if (!g_app.project) {
        fprintf(stderr, "Failed to create project\n");
        return false;
    }
    
    g_app.modified = false;
    printf("Created new project: %s (%dx%d @ %.2f fps)\n", 
           name, res.width, res.height, fps);
    
    return true;
}

static bool app_open_project(const char *path) {
    if (g_app.project) {
        rc_project_destroy(g_app.project);
    }
    
    g_app.project = rc_project_load(path);
    if (!g_app.project) {
        fprintf(stderr, "Failed to open project: %s\n", path);
        return false;
    }
    
    g_app.modified = false;
    printf("Opened project: %s\n", path);
    
    return true;
}

static bool app_save_project(const char *path) {
    if (!g_app.project) {
        fprintf(stderr, "No project to save\n");
        return false;
    }
    
    if (!rc_project_save(g_app.project, path)) {
        fprintf(stderr, "Failed to save project: %s\n", path);
        return false;
    }
    
    g_app.modified = false;
    printf("Saved project: %s\n", path);
    
    return true;
}

/* ============================================================================
 * Media Import
 * ============================================================================ */

static rc_media_t *app_import_media(const char *path) {
    if (!g_app.project) {
        fprintf(stderr, "No project open\n");
        return NULL;
    }
    
    rc_media_t *media = rc_media_import(g_app.project, path);
    if (!media) {
        fprintf(stderr, "Failed to import: %s\n", path);
        return NULL;
    }
    
    rc_media_info_t info;
    if (rc_media_get_info(media, &info)) {
        printf("Imported: %s\n", path);
        printf("  Type: %s\n", info.type == RC_MEDIA_VIDEO ? "Video" : 
                              info.type == RC_MEDIA_AUDIO ? "Audio" : "Image");
        printf("  Duration: %.2f sec\n", (double)info.duration / RC_TIME_SECOND);
        if (info.type == RC_MEDIA_VIDEO) {
            printf("  Resolution: %dx%d @ %.2f fps\n", 
                   info.resolution.width, info.resolution.height, info.fps);
        }
    }
    
    g_app.modified = true;
    return media;
}

/* ============================================================================
 * Export
 * ============================================================================ */

static void export_progress(float progress, const char *status, void *user) {
    (void)user;
    printf("\rExporting: %.1f%% - %s", progress * 100, status);
    fflush(stdout);
}

static bool app_export(rc_export_preset_t preset, const char *output) {
    if (!g_app.project) {
        fprintf(stderr, "No project to export\n");
        return false;
    }
    
    rc_export_settings_t settings;
    rc_export_get_preset(preset, &settings);
    strncpy(settings.output_path, output, sizeof(settings.output_path) - 1);
    
    printf("Exporting to: %s\n", output);
    
    bool success = rc_export_start(g_app.project, &settings, 
                                    export_progress, NULL);
    
    printf("\n");
    
    if (success) {
        printf("Export complete!\n");
    } else {
        fprintf(stderr, "Export failed\n");
    }
    
    return success;
}

/* ============================================================================
 * Command Line Interface
 * ============================================================================ */

static void print_usage(const char *prog) {
    printf("Usage: %s [options] [project.rcproj]\n\n", prog);
    printf("Options:\n");
    printf("  -n, --new NAME     Create new project\n");
    printf("  -r, --resolution   Set resolution (720p, 1080p, 4k)\n");
    printf("  -f, --fps FPS      Set frame rate (24, 30, 60)\n");
    printf("  -i, --import FILE  Import media file\n");
    printf("  -e, --export FILE  Export to file\n");
    printf("  -p, --preset NAME  Export preset (youtube, instagram, tiktok)\n");
    printf("  -h, --help         Show this help\n");
    printf("  -v, --version      Show version\n");
}

static void print_version(void) {
    printf("RoleCut Video Editor %s\n", ROLECUT_VERSION_STRING);
    printf("Copyright (c) 2025 KetiveeAI\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[]) {
    if (!app_init()) {
        return 1;
    }
    
    /* Parse command line */
    const char *project_path = NULL;
    const char *new_name = NULL;
    const char *import_path = NULL;
    const char *export_path = NULL;
    rc_resolution_t resolution = RC_RES_1080P;
    double fps = 30.0;
    rc_export_preset_t preset = RC_PRESET_YOUTUBE_1080P;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--new") == 0) && i + 1 < argc) {
            new_name = argv[++i];
        }
        else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--resolution") == 0) && i + 1 < argc) {
            const char *res = argv[++i];
            if (strcmp(res, "720p") == 0) resolution = RC_RES_720P;
            else if (strcmp(res, "1080p") == 0) resolution = RC_RES_1080P;
            else if (strcmp(res, "1440p") == 0) resolution = RC_RES_1440P;
            else if (strcmp(res, "4k") == 0) resolution = RC_RES_4K;
        }
        else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fps") == 0) && i + 1 < argc) {
            fps = atof(argv[++i]);
        }
        else if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--import") == 0) && i + 1 < argc) {
            import_path = argv[++i];
        }
        else if ((strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--export") == 0) && i + 1 < argc) {
            export_path = argv[++i];
        }
        else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--preset") == 0) && i + 1 < argc) {
            const char *p = argv[++i];
            if (strcmp(p, "youtube") == 0) preset = RC_PRESET_YOUTUBE_1080P;
            else if (strcmp(p, "youtube4k") == 0) preset = RC_PRESET_YOUTUBE_4K;
            else if (strcmp(p, "instagram") == 0) preset = RC_PRESET_INSTAGRAM_FEED;
            else if (strcmp(p, "tiktok") == 0) preset = RC_PRESET_TIKTOK;
            else if (strcmp(p, "twitter") == 0) preset = RC_PRESET_TWITTER;
        }
        else if (argv[i][0] != '-') {
            project_path = argv[i];
        }
    }
    
    /* Create or open project */
    if (new_name) {
        if (!app_new_project(new_name, resolution, fps)) {
            app_shutdown();
            return 1;
        }
    } else if (project_path) {
        if (!app_open_project(project_path)) {
            app_shutdown();
            return 1;
        }
    } else {
        /* No project specified, create default */
        app_new_project("Untitled", resolution, fps);
    }
    
    /* Import media if specified */
    if (import_path) {
        app_import_media(import_path);
    }
    
    /* Export if specified */
    if (export_path) {
        app_export(preset, export_path);
    }
    
    /* TODO: Start UI main loop */
    /* For now, just print project info */
    if (g_app.project) {
        const rc_project_settings_t *settings = rc_project_get_settings(g_app.project);
        printf("\nProject: %s\n", settings->name);
        printf("Resolution: %dx%d\n", settings->resolution.width, settings->resolution.height);
        printf("FPS: %.2f\n", settings->fps);
    }
    
    app_shutdown();
    return 0;
}
