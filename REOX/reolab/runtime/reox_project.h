/*
 * REOX Project Manager
 * Scaffolding for new NeolyxOS applications
 * 
 * When user clicks "Create New Project" in Reolab IDE,
 * this module creates the correct directory structure.
 */

#ifndef REOX_PROJECT_H
#define REOX_PROJECT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Project Types
 * ============================================================================ */

typedef enum {
    RX_PROJECT_CONSOLE,    /* Console application (no UI) */
    RX_PROJECT_GUI,        /* GUI application (uses reox_ui) */
    RX_PROJECT_GAME,       /* Game (uses NXGame Bridge) */
    RX_PROJECT_LIBRARY,    /* Shared library */
    RX_PROJECT_FRAMEWORK   /* Framework bundle */
} rx_project_type;

/* ============================================================================
 * Project Info
 * ============================================================================ */

typedef struct rx_project_info {
    const char* name;           /* App name (e.g., "MyApp") */
    const char* identifier;     /* Bundle ID (e.g., "com.developer.myapp") */
    const char* version;        /* Version string (e.g., "1.0.0") */
    const char* author;         /* Author/organization */
    const char* description;    /* Short description */
    rx_project_type type;       /* Project type */
    const char* location;       /* Parent directory path */
} rx_project_info;

/* ============================================================================
 * Project Creation
 * ============================================================================ */

/**
 * Create a new NeolyxOS application project.
 * 
 * Creates directory structure:
 *   {location}/{name}/
 *   ├── Info.plist        - App metadata
 *   ├── Makefile          - Build script  
 *   ├── README.md         - Documentation
 *   ├── src/
 *   │   └── main.rx       - Main source file
 *   ├── Resources/
 *   │   └── assets/       - Images, sounds
 *   └── lib/              - External libraries
 * 
 * @param info Project configuration
 * @return 0 on success, negative error code on failure
 */
int reolab_project_create(const rx_project_info* info);

/**
 * Create project with default settings.
 * Uses current directory and default identifier.
 */
int reolab_project_quick(const char* name, rx_project_type type);

/**
 * Open existing project.
 */
int reolab_project_open(const char* path);

/**
 * Get last error message.
 */
const char* reolab_project_error(void);

/* ============================================================================
 * Directory Path Helpers (uses NeolyxOS paths from fs_init.h)
 * ============================================================================ */

/**
 * Get user's Applications directory.
 * Returns: /Users/{username}/Applications
 */
const char* reolab_user_apps_path(void);

/**
 * Get user's Developer directory.
 * Returns: /Developer
 */
const char* reolab_developer_path(void);

/**
 * Get template directory.
 * Returns: /Library/Reolab/Templates
 */
const char* reolab_templates_path(void);

#ifdef __cplusplus
}
#endif

#endif /* REOX_PROJECT_H */
