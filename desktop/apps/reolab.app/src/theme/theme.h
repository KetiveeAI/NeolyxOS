/*
 * Reolab - Theme Loader
 * NeolyxOS Application
 * 
 * Loads JSON theme configuration for colors, icons, and editor settings.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef REOLAB_THEME_H
#define REOLAB_THEME_H

#include "../../include/reolab.h"
#include "../graphics/graphics.h"

#define MAX_THEME_NAME 64
#define MAX_ICON_MAPPINGS 128
#define MAX_FOLDER_MAPPINGS 64

/* Theme colors structure */
typedef struct {
    RLColor background;
    RLColor backgroundSecondary;
    RLColor sidebar;
    RLColor toolbar;
    RLColor statusBar;
    RLColor accent;
    RLColor accentSecondary;
    
    RLColor text;
    RLColor textDim;
    RLColor textBright;
    
    RLColor border;
    RLColor selection;
    RLColor cursor;
    RLColor lineHighlight;
} RLThemeColors;

/* Syntax highlighting colors */
typedef struct {
    RLColor keyword;
    RLColor type;
    RLColor function;
    RLColor string;
    RLColor number;
    RLColor comment;
    RLColor preprocessor;
    RLColor macro;
    RLColor operator_;
    RLColor variable;
    RLColor constant;
    RLColor class_;
} RLSyntaxColors;

/* File icon mapping */
typedef struct {
    char extension[16];   /* e.g., "c", "cpp", "rs" */
    char icon[64];        /* e.g., "c.nxi" */
    RLColor color;
} RLFileIconMapping;

/* Folder icon mapping */
typedef struct {
    char name[32];        /* e.g., "src", "include" */
    char icon[64];        /* e.g., "folder-src.nxi" */
    char openIcon[64];    /* e.g., "folder-src-open.nxi" */
    RLColor color;
} RLFolderIconMapping;

/* Editor settings */
typedef struct {
    char fontFamily[64];
    int fontSize;
    float lineHeight;
    int tabSize;
    int cursorStyle;      /* 0=bar, 1=underscore, 2=block */
    int cursorBlinkRate;
    bool showLineNumbers;
    bool showMinimap;
    bool wordWrap;
} RLEditorSettings;

/* Complete theme */
typedef struct {
    char name[MAX_THEME_NAME];
    char version[16];
    char author[64];
    
    RLThemeColors colors;
    RLSyntaxColors syntax;
    RLEditorSettings editor;
    
    RLFileIconMapping fileIcons[MAX_ICON_MAPPINGS];
    int fileIconCount;
    char defaultFileIcon[64];
    
    RLFolderIconMapping folderIcons[MAX_FOLDER_MAPPINGS];
    int folderIconCount;
    char defaultFolderIcon[64];
    char defaultFolderOpenIcon[64];
    RLColor defaultFolderColor;
} RLTheme;

/* Theme API */
RLTheme* rl_theme_load(const char* path);
void rl_theme_destroy(RLTheme* theme);

/* Get icon for file by extension or filename */
const RLFileIconMapping* rl_theme_get_file_icon(RLTheme* theme, const char* filename);
const RLFolderIconMapping* rl_theme_get_folder_icon(RLTheme* theme, const char* foldername);

/* Color utilities */
RLColor rl_theme_parse_color(const char* hex);

/* Default theme (built-in, no file needed) */
RLTheme* rl_theme_default(void);

#endif /* REOLAB_THEME_H */
