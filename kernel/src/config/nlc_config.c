/*
 * NeolyxOS NLC Configuration Parser/Writer
 * 
 * Parses and writes .nlc (NeoLyx Config) files
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../../include/config/nlc_config.h"
#include "../../include/fs/vfs.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Global Config Instances ============ */

nlc_config_t g_system_config = {0};
nlc_config_t g_boot_config = {0};

/* ============ String Utilities ============ */

static int nlc_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void nlc_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int nlc_strcmp(const char *a, const char *b) {
    if (!a || !b) return a != b;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void nlc_trim(char *s) {
    /* Trim leading whitespace */
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;
    
    if (start != s) {
        int i = 0;
        while (start[i]) { s[i] = start[i]; i++; }
        s[i] = '\0';
    }
    
    /* Trim trailing whitespace */
    int len = nlc_strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

/* ============ Parser ============ */

static nlc_section_t* nlc_find_section(nlc_config_t *config, const char *name) {
    for (int i = 0; i < config->section_count; i++) {
        if (nlc_strcmp(config->sections[i].name, name) == 0) {
            return &config->sections[i];
        }
    }
    return NULL;
}

static nlc_section_t* nlc_add_section(nlc_config_t *config, const char *name) {
    if (config->section_count >= NLC_MAX_SECTIONS) return NULL;
    
    nlc_section_t *sec = &config->sections[config->section_count++];
    nlc_strcpy(sec->name, name, NLC_MAX_SECTION_LEN);
    sec->entry_count = 0;
    return sec;
}

static int nlc_parse_line(nlc_config_t *config, char *line, nlc_section_t **current_section) {
    nlc_trim(line);
    
    /* Skip empty lines and comments */
    if (line[0] == '\0' || line[0] == '#') return 0;
    
    /* Section header [name] */
    if (line[0] == '[') {
        char *end = line + 1;
        while (*end && *end != ']') end++;
        if (*end == ']') {
            *end = '\0';
            *current_section = nlc_find_section(config, line + 1);
            if (!*current_section) {
                *current_section = nlc_add_section(config, line + 1);
            }
        }
        return 0;
    }
    
    /* Key = "value" */
    if (*current_section) {
        char *eq = line;
        while (*eq && *eq != '=') eq++;
        
        if (*eq == '=') {
            *eq = '\0';
            char *key = line;
            char *value = eq + 1;
            
            nlc_trim(key);
            nlc_trim(value);
            
            /* Remove quotes from value */
            int vlen = nlc_strlen(value);
            if (vlen >= 2 && value[0] == '"' && value[vlen-1] == '"') {
                value[vlen-1] = '\0';
                value++;
            }
            
            /* Add entry */
            if ((*current_section)->entry_count < NLC_MAX_KEYS) {
                nlc_entry_t *entry = &(*current_section)->entries[(*current_section)->entry_count++];
                nlc_strcpy(entry->key, key, NLC_MAX_KEY_LEN);
                nlc_strcpy(entry->value, value, NLC_MAX_VALUE_LEN);
            }
        }
    }
    
    return 0;
}

/* ============ API Functions ============ */

int nlc_init(void) {
    serial_puts("[NLC] Initializing config system...\\n");
    
    /* Load system config */
    if (nlc_load(&g_system_config, "/System/Config/system.nlc") != 0) {
        /* Try development path */
        nlc_load(&g_system_config, "/EFI/BOOT/config/system.nlc");
    }
    
    /* Load boot config */
    if (nlc_load(&g_boot_config, "/System/Config/boot.nlc") != 0) {
        /* Try development path */
        nlc_load(&g_boot_config, "/EFI/BOOT/config/boot.nlc");
    }
    
    serial_puts("[NLC] Config system ready\\n");
    return 0;
}

int nlc_load(nlc_config_t *config, const char *path) {
    if (!config || !path) return -1;
    
    serial_puts("[NLC] Loading ");
    serial_puts(path);
    serial_puts("\\n");
    
    vfs_file_t *file = vfs_open(path, 0);  /* O_RDONLY */
    if (!file) {
        serial_puts("[NLC] File not found\\n");
        return -1;
    }
    
    nlc_strcpy(config->filepath, path, 256);
    config->section_count = 0;
    config->loaded = 0;
    
    /* Read file content */
    char buffer[4096];
    int bytes = vfs_read(file, buffer, sizeof(buffer) - 1);
    vfs_close(file);
    
    if (bytes <= 0) {
        serial_puts("[NLC] Empty or unreadable\\n");
        return -1;
    }
    
    buffer[bytes] = '\0';
    
    /* Parse line by line */
    nlc_section_t *current = NULL;
    char line[512];
    int line_idx = 0;
    
    for (int i = 0; i <= bytes; i++) {
        char c = buffer[i];
        if (c == '\n' || c == '\r' || c == '\0') {
            line[line_idx] = '\0';
            if (line_idx > 0) {
                nlc_parse_line(config, line, &current);
            }
            line_idx = 0;
        } else if (line_idx < 511) {
            line[line_idx++] = c;
        }
    }
    
    config->loaded = 1;
    serial_puts("[NLC] Loaded ");
    char num[16];
    int n = config->section_count;
    if (n == 0) { serial_putc('0'); }
    else {
        int idx = 0;
        while (n > 0) { num[idx++] = '0' + (n % 10); n /= 10; }
        while (idx > 0) { serial_putc(num[--idx]); }
    }
    serial_puts(" sections\\n");
    
    return 0;
}

int nlc_save(nlc_config_t *config) {
    if (!config || !config->filepath[0]) return -1;
    
    serial_puts("[NLC] Saving ");
    serial_puts(config->filepath);
    serial_puts("\\n");
    
    /* Build content */
    char buffer[8192];
    int pos = 0;
    
    /* Header */
    const char *header = "# NeolyxOS Configuration File\\n# Auto-generated\\n\\n";
    for (int i = 0; header[i] && pos < 8100; i++) {
        buffer[pos++] = header[i];
    }
    
    /* Sections */
    for (int s = 0; s < config->section_count && pos < 8000; s++) {
        nlc_section_t *sec = &config->sections[s];
        
        /* Section header */
        buffer[pos++] = '[';
        for (int i = 0; sec->name[i] && pos < 8100; i++) {
            buffer[pos++] = sec->name[i];
        }
        buffer[pos++] = ']';
        buffer[pos++] = '\n';
        
        /* Entries */
        for (int e = 0; e < sec->entry_count && pos < 8000; e++) {
            nlc_entry_t *entry = &sec->entries[e];
            
            for (int i = 0; entry->key[i] && pos < 8100; i++) {
                buffer[pos++] = entry->key[i];
            }
            buffer[pos++] = ' ';
            buffer[pos++] = '=';
            buffer[pos++] = ' ';
            buffer[pos++] = '"';
            for (int i = 0; entry->value[i] && pos < 8100; i++) {
                buffer[pos++] = entry->value[i];
            }
            buffer[pos++] = '"';
            buffer[pos++] = '\n';
        }
        
        buffer[pos++] = '\n';
    }
    
    buffer[pos] = '\0';
    
    /* Write to file */
    vfs_create(config->filepath, 1, 0644);  /* VFS_FILE */
    vfs_file_t *file = vfs_open(config->filepath, 0x0102);  /* O_WRONLY|O_CREATE */
    if (!file) {
        serial_puts("[NLC] Failed to create file\\n");
        return -1;
    }
    
    vfs_write(file, buffer, pos);
    vfs_close(file);
    
    serial_puts("[NLC] Saved successfully\\n");
    return 0;
}

const char* nlc_get(nlc_config_t *config, const char *section, const char *key) {
    if (!config || !section || !key) return NULL;
    
    nlc_section_t *sec = nlc_find_section(config, section);
    if (!sec) return NULL;
    
    for (int i = 0; i < sec->entry_count; i++) {
        if (nlc_strcmp(sec->entries[i].key, key) == 0) {
            return sec->entries[i].value;
        }
    }
    
    return NULL;
}

int nlc_set(nlc_config_t *config, const char *section, const char *key, const char *value) {
    if (!config || !section || !key || !value) return -1;
    
    nlc_section_t *sec = nlc_find_section(config, section);
    if (!sec) {
        sec = nlc_add_section(config, section);
        if (!sec) return -1;
    }
    
    /* Find existing entry */
    for (int i = 0; i < sec->entry_count; i++) {
        if (nlc_strcmp(sec->entries[i].key, key) == 0) {
            nlc_strcpy(sec->entries[i].value, value, NLC_MAX_VALUE_LEN);
            return 0;
        }
    }
    
    /* Add new entry */
    if (sec->entry_count >= NLC_MAX_KEYS) return -1;
    
    nlc_entry_t *entry = &sec->entries[sec->entry_count++];
    nlc_strcpy(entry->key, key, NLC_MAX_KEY_LEN);
    nlc_strcpy(entry->value, value, NLC_MAX_VALUE_LEN);
    
    return 0;
}

int nlc_get_int(nlc_config_t *config, const char *section, const char *key, int default_val) {
    const char *val = nlc_get(config, section, key);
    if (!val) return default_val;
    
    int result = 0;
    int neg = 0;
    const char *p = val;
    
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') {
        result = result * 10 + (*p - '0');
        p++;
    }
    
    return neg ? -result : result;
}

int nlc_get_bool(nlc_config_t *config, const char *section, const char *key, int default_val) {
    const char *val = nlc_get(config, section, key);
    if (!val) return default_val;
    
    if (nlc_strcmp(val, "true") == 0 || nlc_strcmp(val, "1") == 0 ||
        nlc_strcmp(val, "yes") == 0 || nlc_strcmp(val, "on") == 0) {
        return 1;
    }
    
    return 0;
}

/* ============ Convenience Functions ============ */

const char* nlc_get_os_name(void) {
    const char *name = nlc_get(&g_system_config, "system", "name");
    return name ? name : NEOLYX_OS_NAME;
}

const char* nlc_get_os_version(void) {
    const char *ver = nlc_get(&g_system_config, "system", "version");
    return ver ? ver : NEOLYX_VERSION;
}

const char* nlc_get_os_codename(void) {
    const char *code = nlc_get(&g_system_config, "system", "codename");
    return code ? code : NEOLYX_CODENAME;
}

void nlc_get_full_version(char *buffer, int max_len) {
    if (!buffer || max_len < 32) return;
    
    const char *name = nlc_get_os_name();
    const char *ver = nlc_get_os_version();
    const char *code = nlc_get_os_codename();
    
    int pos = 0;
    
    /* Name */
    for (int i = 0; name[i] && pos < max_len - 20; i++) {
        buffer[pos++] = name[i];
    }
    buffer[pos++] = ' ';
    
    /* Version */
    for (int i = 0; ver[i] && pos < max_len - 15; i++) {
        buffer[pos++] = ver[i];
    }
    buffer[pos++] = ' ';
    
    /* Codename */
    for (int i = 0; code[i] && pos < max_len - 1; i++) {
        buffer[pos++] = code[i];
    }
    
    buffer[pos] = '\0';
}

int nlc_is_installed(void) {
    const char *installed = nlc_get(&g_boot_config, "installation", "installed");
    if (installed && nlc_strcmp(installed, "true") == 0) {
        return 1;
    }
    return 0;
}

const char* nlc_get_boot_device(void) {
    return nlc_get(&g_boot_config, "disk", "boot_device");
}

const char* nlc_get_edition(void) {
    const char *edition = nlc_get(&g_boot_config, "installation", "edition");
    return edition ? edition : "desktop";
}
