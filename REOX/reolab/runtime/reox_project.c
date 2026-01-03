/*
 * REOX Project Manager - Implementation
 * Scaffolding for new NeolyxOS applications
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "reox_project.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ============================================================================
 * Error Handling
 * ============================================================================ */

static char last_error[256] = "";

static void set_error(const char* fmt, const char* detail) {
    snprintf(last_error, sizeof(last_error), fmt, detail);
}

const char* reolab_project_error(void) {
    return last_error;
}

/* ============================================================================
 * File System Helpers
 * ============================================================================ */

static int create_directory(const char* path) {
    if (mkdir(path, 0755) != 0) {
        if (errno != EEXIST) {
            set_error("Failed to create directory: %s", path);
            return -1;
        }
    }
    return 0;
}

static int write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) {
        set_error("Failed to create file: %s", path);
        return -1;
    }
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

/* ============================================================================
 * Template: Info.plist
 * ============================================================================ */

static int create_info_plist(const char* base_path, const rx_project_info* info) {
    char path[512];
    snprintf(path, sizeof(path), "%s/Info.plist", base_path);
    
    char content[2048];
    snprintf(content, sizeof(content),
        "{\n"
        "    \"NXAppName\": \"%s\",\n"
        "    \"NXAppId\": \"%s\",\n"
        "    \"NXAppVersion\": \"%s\",\n"
        "    \"NXExecutable\": \"%s\",\n"
        "    \"NXIcon\": \"Resources/icon.png\",\n"
        "    \n"
        "    \"NXAuthor\": \"%s\",\n"
        "    \"NXDescription\": \"%s\",\n"
        "    \n"
        "    \"NXCapabilities\": [\n"
        "        \"filesystem.user\"%s\n"
        "    ],\n"
        "    \n"
        "    \"NXMinOS\": \"1.0\"\n"
        "}\n",
        info->name,
        info->identifier,
        info->version,
        info->name,
        info->author ? info->author : "Developer",
        info->description ? info->description : "",
        info->type == RX_PROJECT_GUI ? ",\n        \"gpu.render\"" :
        info->type == RX_PROJECT_GAME ? ",\n        \"gpu.render\",\n        \"audio.playback\",\n        \"nxgame.session\"" : ""
    );
    
    return write_file(path, content);
}

/* ============================================================================
 * Template: main.rx (REOX source)
 * ============================================================================ */

static const char* get_main_template(rx_project_type type) {
    switch (type) {
        case RX_PROJECT_CONSOLE:
            return 
                "// %s - NeolyxOS Console Application\n"
                "// Created by Reolab\n"
                "\n"
                "fn main() {\n"
                "    println(\"Hello, NeolyxOS!\")\n"
                "}\n";
        
        case RX_PROJECT_GUI:
            return
                "// %s - NeolyxOS GUI Application\n"
                "// Created by Reolab\n"
                "\n"
                "import ui\n"
                "\n"
                "fn main() {\n"
                "    let window = ui.window(\"%s\", 800, 600)\n"
                "    \n"
                "    let label = ui.text(\"Hello, NeolyxOS!\")\n"
                "    label.center()\n"
                "    window.add(label)\n"
                "    \n"
                "    ui.run()\n"
                "}\n";
        
        case RX_PROJECT_GAME:
            return
                "// %s - NeolyxOS Game\n"
                "// Created by Reolab\n"
                "\n"
                "import nxgame\n"
                "import ui\n"
                "\n"
                "fn main() {\n"
                "    // Initialize NXGame session\n"
                "    let session = nxgame.session_start()\n"
                "    \n"
                "    // Game loop\n"
                "    while session.running {\n"
                "        // Poll input\n"
                "        let input = nxgame.poll_input()\n"
                "        \n"
                "        // Update game state\n"
                "        update(input)\n"
                "        \n"
                "        // Render frame\n"
                "        render(session)\n"
                "    }\n"
                "    \n"
                "    nxgame.session_end(session)\n"
                "}\n"
                "\n"
                "fn update(input: Input) {\n"
                "    // TODO: Game logic\n"
                "}\n"
                "\n"
                "fn render(session: Session) {\n"
                "    // TODO: Render graphics\n"
                "    nxgame.clear(0x1a1a2e)\n"
                "    nxgame.present()\n"
                "}\n";
        
        case RX_PROJECT_LIBRARY:
            return
                "// %s - NeolyxOS Library\n"
                "// Created by Reolab\n"
                "\n"
                "pub fn hello() -> String {\n"
                "    return \"Hello from %s!\"\n"
                "}\n";
        
        default:
            return "// %s\nfn main() {}\n";
    }
}

static int create_main_rx(const char* base_path, const rx_project_info* info) {
    char path[512];
    snprintf(path, sizeof(path), "%s/src/main.rx", base_path);
    
    char content[4096];
    snprintf(content, sizeof(content), get_main_template(info->type),
             info->name, info->name, info->name);
    
    return write_file(path, content);
}

/* ============================================================================
 * Template: Makefile
 * ============================================================================ */

static int create_makefile(const char* base_path, const rx_project_info* info) {
    char path[512];
    snprintf(path, sizeof(path), "%s/Makefile", base_path);
    
    char content[2048];
    snprintf(content, sizeof(content),
        "# %s - NeolyxOS Application Makefile\n"
        "# Generated by Reolab\n"
        "\n"
        "APP_NAME := %s\n"
        "REOXC := reoxc\n"
        "CC := gcc\n"
        "RUNTIME := /Library/Reolab/runtime\n"
        "\n"
        "SRC := src/main.rx\n"
        "OUT := $(APP_NAME)\n"
        "\n"
        ".PHONY: all clean run\n"
        "\n"
        "all: $(OUT)\n"
        "\n"
        "$(OUT): $(SRC)\n"
        "\t$(REOXC) $(SRC) --emit exe -o $(OUT) -O2 --lto --runtime $(RUNTIME)\n"
        "\n"
        "debug: $(SRC)\n"
        "\t$(REOXC) $(SRC) --emit exe -o $(OUT) -O0 --runtime $(RUNTIME)\n"
        "\n"
        "clean:\n"
        "\trm -f $(OUT) src/*.c\n"
        "\n"
        "run: $(OUT)\n"
        "\t./$(OUT)\n",
        info->name, info->name
    );
    
    return write_file(path, content);
}

/* ============================================================================
 * Template: README.md
 * ============================================================================ */

static int create_readme(const char* base_path, const rx_project_info* info) {
    char path[512];
    snprintf(path, sizeof(path), "%s/README.md", base_path);
    
    const char* type_str = 
        info->type == RX_PROJECT_CONSOLE ? "Console Application" :
        info->type == RX_PROJECT_GUI ? "GUI Application" :
        info->type == RX_PROJECT_GAME ? "Game" :
        info->type == RX_PROJECT_LIBRARY ? "Library" : "Application";
    
    char content[2048];
    snprintf(content, sizeof(content),
        "# %s\n"
        "\n"
        "%s\n"
        "\n"
        "A NeolyxOS %s created with Reolab.\n"
        "\n"
        "## Build\n"
        "\n"
        "```bash\n"
        "make\n"
        "```\n"
        "\n"
        "## Run\n"
        "\n"
        "```bash\n"
        "make run\n"
        "# or\n"
        "./%s\n"
        "```\n"
        "\n"
        "## Structure\n"
        "\n"
        "```\n"
        "%s/\n"
        "├── Info.plist      # App metadata\n"
        "├── Makefile        # Build script\n"
        "├── src/\n"
        "│   └── main.rx     # Main source (REOX)\n"
        "├── Resources/\n"
        "│   └── assets/     # Images, sounds\n"
        "└── lib/            # Libraries\n"
        "```\n"
        "\n"
        "## License\n"
        "\n"
        "© %s\n",
        info->name,
        info->description ? info->description : "",
        type_str,
        info->name,
        info->name,
        info->author ? info->author : "Developer"
    );
    
    return write_file(path, content);
}

/* ============================================================================
 * Main Project Creation
 * ============================================================================ */

int reolab_project_create(const rx_project_info* info) {
    if (!info || !info->name || !info->location) {
        set_error("Invalid project info: %s", "name and location required");
        return -1;
    }
    
    char base_path[512];
    snprintf(base_path, sizeof(base_path), "%s/%s", info->location, info->name);
    
    printf("Creating project: %s\n", info->name);
    printf("Location: %s\n\n", base_path);
    
    /* Create main directory */
    if (create_directory(base_path) != 0) return -1;
    printf("  [+] %s/\n", info->name);
    
    /* Create subdirectories */
    char path[512];
    
    snprintf(path, sizeof(path), "%s/src", base_path);
    if (create_directory(path) != 0) return -1;
    printf("  [+] src/\n");
    
    snprintf(path, sizeof(path), "%s/Resources", base_path);
    if (create_directory(path) != 0) return -1;
    printf("  [+] Resources/\n");
    
    snprintf(path, sizeof(path), "%s/Resources/assets", base_path);
    if (create_directory(path) != 0) return -1;
    printf("  [+] Resources/assets/\n");
    
    snprintf(path, sizeof(path), "%s/lib", base_path);
    if (create_directory(path) != 0) return -1;
    printf("  [+] lib/\n");
    
    /* Create files */
    if (create_info_plist(base_path, info) != 0) return -1;
    printf("  [+] Info.plist\n");
    
    if (create_main_rx(base_path, info) != 0) return -1;
    printf("  [+] src/main.rx\n");
    
    if (create_makefile(base_path, info) != 0) return -1;
    printf("  [+] Makefile\n");
    
    if (create_readme(base_path, info) != 0) return -1;
    printf("  [+] README.md\n");
    
    printf("\nProject created successfully!\n");
    printf("\nNext steps:\n");
    printf("  cd %s\n", base_path);
    printf("  make\n");
    printf("  make run\n");
    
    return 0;
}

/* Quick project creation */
int reolab_project_quick(const char* name, rx_project_type type) {
    rx_project_info info = {0};
    info.name = name;
    info.identifier = "com.developer.app";
    info.version = "1.0.0";
    info.author = "Developer";
    info.type = type;
    info.location = ".";  /* Current directory */
    
    return reolab_project_create(&info);
}

/* Path helpers */
const char* reolab_user_apps_path(void) {
    /* TODO: Get actual username from session */
    return "/Users/user/Applications";
}

const char* reolab_developer_path(void) {
    return "/Developer";
}

const char* reolab_templates_path(void) {
    return "/Library/Reolab/Templates";
}
