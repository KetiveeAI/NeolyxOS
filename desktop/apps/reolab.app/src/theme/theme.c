/*
 * Reolab - Theme Loader Implementation
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Parse hex color string to RLColor */
RLColor rl_theme_parse_color(const char* hex) {
    if (!hex || hex[0] != '#') {
        return RL_HEX(0x000000);
    }
    
    unsigned int val = 0;
    sscanf(hex + 1, "%x", &val);
    
    return (RLColor){
        (val >> 16) & 0xFF,
        (val >> 8) & 0xFF,
        val & 0xFF,
        255
    };
}

/* Helper to get string value from simple JSON (minimal parser) */
static bool json_get_string(const char* json, const char* key, char* out, size_t out_size) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    
    const char* pos = strstr(json, search);
    if (!pos) return false;
    
    pos = strchr(pos, ':');
    if (!pos) return false;
    
    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;
    if (*pos != '"') return false;
    pos++; /* Skip opening quote */
    
    size_t i = 0;
    while (*pos && *pos != '"' && i < out_size - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return true;
}

/* Helper to get nested color value */
static RLColor json_get_color(const char* json, const char* section, const char* key) {
    const char* sec_start = strstr(json, section);
    if (!sec_start) return RL_HEX(0x000000);
    
    char color_str[16] = {0};
    if (json_get_string(sec_start, key, color_str, sizeof(color_str))) {
        return rl_theme_parse_color(color_str);
    }
    return RL_HEX(0x000000);
}

RLTheme* rl_theme_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[Theme] Cannot open: %s\n", path);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* json = (char*)malloc(size + 1);
    fread(json, 1, size, f);
    json[size] = '\0';
    fclose(f);
    
    RLTheme* theme = (RLTheme*)calloc(1, sizeof(RLTheme));
    
    /* Parse basic info */
    json_get_string(json, "name", theme->name, sizeof(theme->name));
    json_get_string(json, "version", theme->version, sizeof(theme->version));
    json_get_string(json, "author", theme->author, sizeof(theme->author));
    
    /* Parse colors */
    theme->colors.background = json_get_color(json, "colors", "background");
    theme->colors.backgroundSecondary = json_get_color(json, "colors", "backgroundSecondary");
    theme->colors.sidebar = json_get_color(json, "colors", "sidebar");
    theme->colors.toolbar = json_get_color(json, "colors", "toolbar");
    theme->colors.statusBar = json_get_color(json, "colors", "statusBar");
    theme->colors.accent = json_get_color(json, "colors", "accent");
    theme->colors.text = json_get_color(json, "colors", "text");
    theme->colors.textDim = json_get_color(json, "colors", "textDim");
    theme->colors.border = json_get_color(json, "colors", "border");
    theme->colors.selection = json_get_color(json, "colors", "selection");
    theme->colors.cursor = json_get_color(json, "colors", "cursor");
    
    /* Parse syntax colors */
    theme->syntax.keyword = json_get_color(json, "syntax", "keyword");
    theme->syntax.type = json_get_color(json, "syntax", "type");
    theme->syntax.function = json_get_color(json, "syntax", "function");
    theme->syntax.string = json_get_color(json, "syntax", "string");
    theme->syntax.number = json_get_color(json, "syntax", "number");
    theme->syntax.comment = json_get_color(json, "syntax", "comment");
    theme->syntax.preprocessor = json_get_color(json, "syntax", "preprocessor");
    theme->syntax.operator_ = json_get_color(json, "syntax", "operator");
    
    /* Default icons */
    strcpy(theme->defaultFileIcon, "file.nxi");
    strcpy(theme->defaultFolderIcon, "folder.nxi");
    strcpy(theme->defaultFolderOpenIcon, "folder-open.nxi");
    theme->defaultFolderColor = RL_HEX(0x8B949E);
    
    /* TODO: Parse file and folder icon mappings from JSON arrays */
    /* For now, add some hardcoded defaults */
    theme->fileIconCount = 0;
    
    /* C files */
    strcpy(theme->fileIcons[theme->fileIconCount].extension, "c");
    strcpy(theme->fileIcons[theme->fileIconCount].icon, "c.nxi");
    theme->fileIcons[theme->fileIconCount].color = RL_HEX(0xA8B9CC);
    theme->fileIconCount++;
    
    /* Header files */
    strcpy(theme->fileIcons[theme->fileIconCount].extension, "h");
    strcpy(theme->fileIcons[theme->fileIconCount].icon, "h.nxi");
    theme->fileIcons[theme->fileIconCount].color = RL_HEX(0x6D8086);
    theme->fileIconCount++;
    
    /* REOX files */
    strcpy(theme->fileIcons[theme->fileIconCount].extension, "reox");
    strcpy(theme->fileIcons[theme->fileIconCount].icon, "reox.nxi");
    theme->fileIcons[theme->fileIconCount].color = RL_HEX(0x8957E5);
    theme->fileIconCount++;
    
    /* Rust files */
    strcpy(theme->fileIcons[theme->fileIconCount].extension, "rs");
    strcpy(theme->fileIcons[theme->fileIconCount].icon, "rust.nxi");
    theme->fileIcons[theme->fileIconCount].color = RL_HEX(0xDEA584);
    theme->fileIconCount++;
    
    /* JSON files */
    strcpy(theme->fileIcons[theme->fileIconCount].extension, "json");
    strcpy(theme->fileIcons[theme->fileIconCount].icon, "json.nxi");
    theme->fileIcons[theme->fileIconCount].color = RL_HEX(0xCBCB41);
    theme->fileIconCount++;
    
    /* Folder mappings */
    theme->folderIconCount = 0;
    
    strcpy(theme->folderIcons[theme->folderIconCount].name, "src");
    strcpy(theme->folderIcons[theme->folderIconCount].icon, "folder-src.nxi");
    theme->folderIcons[theme->folderIconCount].color = RL_HEX(0x42A5F5);
    theme->folderIconCount++;
    
    strcpy(theme->folderIcons[theme->folderIconCount].name, "include");
    strcpy(theme->folderIcons[theme->folderIconCount].icon, "folder-include.nxi");
    theme->folderIcons[theme->folderIconCount].color = RL_HEX(0x78909C);
    theme->folderIconCount++;
    
    strcpy(theme->folderIcons[theme->folderIconCount].name, "ui");
    strcpy(theme->folderIcons[theme->folderIconCount].icon, "folder-ui.nxi");
    theme->folderIcons[theme->folderIconCount].color = RL_HEX(0xAB47BC);
    theme->folderIconCount++;
    
    free(json);
    printf("[Theme] Loaded: %s v%s\n", theme->name, theme->version);
    return theme;
}

void rl_theme_destroy(RLTheme* theme) {
    if (theme) free(theme);
}

const RLFileIconMapping* rl_theme_get_file_icon(RLTheme* theme, const char* filename) {
    if (!theme || !filename) return NULL;
    
    /* Find extension */
    const char* ext = strrchr(filename, '.');
    if (ext) ext++; else return NULL;
    
    /* Search by extension */
    for (int i = 0; i < theme->fileIconCount; i++) {
        if (strcasecmp(theme->fileIcons[i].extension, ext) == 0) {
            return &theme->fileIcons[i];
        }
    }
    
    return NULL;
}

const RLFolderIconMapping* rl_theme_get_folder_icon(RLTheme* theme, const char* foldername) {
    if (!theme || !foldername) return NULL;
    
    for (int i = 0; i < theme->folderIconCount; i++) {
        if (strcasecmp(theme->folderIcons[i].name, foldername) == 0) {
            return &theme->folderIcons[i];
        }
    }
    
    return NULL;
}

/* Default built-in theme (no file loading needed) */
RLTheme* rl_theme_default(void) {
    RLTheme* theme = (RLTheme*)calloc(1, sizeof(RLTheme));
    
    strcpy(theme->name, "Material Dark (Built-in)");
    strcpy(theme->version, "1.0.0");
    strcpy(theme->author, "NeolyxOS");
    
    /* Colors - GitHub Dark style */
    theme->colors.background = RL_HEX(0x0D1117);
    theme->colors.backgroundSecondary = RL_HEX(0x161B22);
    theme->colors.sidebar = RL_HEX(0x21262D);
    theme->colors.toolbar = RL_HEX(0x161B22);
    theme->colors.statusBar = RL_HEX(0x238636);
    theme->colors.accent = RL_HEX(0x1F6FEB);
    theme->colors.accentSecondary = RL_HEX(0x8957E5);
    theme->colors.text = RL_HEX(0xE6EDF3);
    theme->colors.textDim = RL_HEX(0x7D8590);
    theme->colors.textBright = RL_HEX(0xFFFFFF);
    theme->colors.border = RL_HEX(0x30363D);
    theme->colors.selection = RL_HEX(0x264F78);
    theme->colors.cursor = RL_HEX(0x58A6FF);
    theme->colors.lineHighlight = RL_HEX(0x161B22);
    
    /* Syntax */
    theme->syntax.keyword = RL_HEX(0xFF7B72);
    theme->syntax.type = RL_HEX(0x79C0FF);
    theme->syntax.function = RL_HEX(0xD2A8FF);
    theme->syntax.string = RL_HEX(0xA5D6FF);
    theme->syntax.number = RL_HEX(0x79C0FF);
    theme->syntax.comment = RL_HEX(0x8B949E);
    theme->syntax.preprocessor = RL_HEX(0xFFA657);
    theme->syntax.macro = RL_HEX(0x79C0FF);
    theme->syntax.operator_ = RL_HEX(0xFF7B72);
    theme->syntax.variable = RL_HEX(0xFFA657);
    theme->syntax.constant = RL_HEX(0x79C0FF);
    theme->syntax.class_ = RL_HEX(0xF0883E);
    
    /* Editor */
    strcpy(theme->editor.fontFamily, "NeolyxMono");
    theme->editor.fontSize = 14;
    theme->editor.lineHeight = 1.5f;
    theme->editor.tabSize = 4;
    theme->editor.cursorStyle = 0; /* bar */
    theme->editor.cursorBlinkRate = 530;
    theme->editor.showLineNumbers = true;
    theme->editor.showMinimap = false;
    theme->editor.wordWrap = false;
    
    /* Default icons */
    strcpy(theme->defaultFileIcon, "file.nxi");
    strcpy(theme->defaultFolderIcon, "folder.nxi");
    
    return theme;
}
