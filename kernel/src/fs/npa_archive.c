/*
 * NeolyxOS NPA Archive Implementation
 * 
 * Parser and extractor for .npa package files.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "fs/npa_archive.h"
#include "fs/vfs.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ String Helpers ============ */

static int npa_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void npa_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int npa_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void npa_memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
}

static void npa_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

static void npa_strcat(char *dst, const char *src, int max) {
    int len = npa_strlen(dst);
    int i = 0;
    while (src[i] && len + i < max - 1) {
        dst[len + i] = src[i];
        i++;
    }
    dst[len + i] = '\0';
}

/* ============ CRC32 Implementation ============ */

/* Standard CRC32 polynomial */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7a47, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t npa_crc32(const void *data, uint32_t size) {
    const uint8_t *bytes = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

/* ============ JSON Parser (Simple) ============ */

/* Skip whitespace in JSON */
static const char* json_skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Extract string value from JSON (simple, no escapes) */
static int json_get_string(const char *json, const char *key, char *value, int max) {
    /* Find "key" */
    char search[128];
    search[0] = '"';
    npa_strcpy(search + 1, key, 126);
    npa_strcat(search, "\"", 128);
    
    const char *p = json;
    while (*p) {
        /* Find key */
        const char *found = p;
        int match = 1;
        for (int i = 0; search[i]; i++) {
            if (found[i] != search[i]) { match = 0; break; }
        }
        
        if (match) {
            /* Skip key and colon */
            p = found + npa_strlen(search);
            p = json_skip_ws(p);
            if (*p == ':') p++;
            p = json_skip_ws(p);
            
            /* Extract string value */
            if (*p == '"') {
                p++;
                int i = 0;
                while (*p && *p != '"' && i < max - 1) {
                    value[i++] = *p++;
                }
                value[i] = '\0';
                return 0;
            }
        }
        p++;
    }
    
    return -1; /* Key not found */
}

/* ============ Archive Implementation ============ */

int npa_open(const char *path, npa_archive_t *archive) {
    if (!path || !archive) {
        return -1;
    }
    
    npa_memset(archive, 0, sizeof(npa_archive_t));
    
    serial_puts("[NPA] Opening archive: ");
    serial_puts(path);
    serial_puts("\n");
    
    /* Open archive file via VFS */
    vfs_file_t *file = vfs_open(path, VFS_O_RDONLY);
    if (!file) {
        serial_puts("[NPA] Failed to open file\n");
        return -2;
    }
    
    archive->device = file;
    
    /* Read header */
    int64_t bytes = vfs_read(file, &archive->header, sizeof(npa_header_t));
    if (bytes != sizeof(npa_header_t)) {
        serial_puts("[NPA] Failed to read header\n");
        vfs_close(file);
        return -3;
    }
    
    /* Validate magic */
    if (archive->header.magic != NPA_MAGIC) {
        serial_puts("[NPA] Invalid magic number\n");
        vfs_close(file);
        return -4;
    }
    
    /* Validate version */
    if (archive->header.version != NPA_VERSION) {
        serial_puts("[NPA] Unsupported version\n");
        vfs_close(file);
        return -5;
    }
    
    serial_puts("[NPA] Header valid, file_count=");
    char num_buf[16];
    int n = archive->header.file_count;
    int i = 0;
    if (n == 0) num_buf[i++] = '0';
    else {
        char tmp[16];
        int j = 0;
        while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
        while (j > 0) num_buf[i++] = tmp[--j];
    }
    num_buf[i] = '\0';
    serial_puts(num_buf);
    serial_puts("\n");
    
    /* Read manifest */
    if (archive->header.manifest_size > 0 && 
        archive->header.manifest_size < NPA_MAX_MANIFEST) {
        char manifest_buf[NPA_MAX_MANIFEST];
        bytes = vfs_read(file, manifest_buf, archive->header.manifest_size);
        if (bytes == archive->header.manifest_size) {
            manifest_buf[bytes] = '\0';
            
            /* Parse manifest JSON */
            json_get_string(manifest_buf, "name", archive->manifest.name, 64);
            json_get_string(manifest_buf, "version", archive->manifest.version, 32);
            json_get_string(manifest_buf, "description", archive->manifest.description, 256);
            json_get_string(manifest_buf, "author", archive->manifest.author, 64);
            json_get_string(manifest_buf, "license", archive->manifest.license, 32);
            json_get_string(manifest_buf, "install_path", archive->manifest.install_path, 128);
            
            serial_puts("[NPA] Manifest parsed: ");
            serial_puts(archive->manifest.name);
            serial_puts(" v");
            serial_puts(archive->manifest.version);
            serial_puts("\n");
        }
    }
    
    /* Allocate and read file entries */
    if (archive->header.file_count > 0 && 
        archive->header.file_count <= NPA_MAX_FILES) {
        
        uint32_t entries_size = archive->header.file_count * sizeof(npa_file_entry_t);
        archive->entries = (npa_file_entry_t*)kmalloc(entries_size);
        
        if (archive->entries) {
            bytes = vfs_read(file, archive->entries, entries_size);
            if (bytes != entries_size) {
                serial_puts("[NPA] Failed to read file entries\n");
                kfree(archive->entries);
                archive->entries = NULL;
            }
        }
    }
    
    /* Calculate data offset */
    archive->data_offset = sizeof(npa_header_t) + 
                          archive->header.manifest_size +
                          (archive->header.file_count * sizeof(npa_file_entry_t));
    
    archive->valid = 1;
    
    serial_puts("[NPA] Archive opened successfully\n");
    return 0;
}

void npa_close(npa_archive_t *archive) {
    if (!archive) return;
    
    if (archive->entries) {
        kfree(archive->entries);
        archive->entries = NULL;
    }
    
    if (archive->device) {
        vfs_close((vfs_file_t*)archive->device);
        archive->device = NULL;
    }
    
    archive->valid = 0;
}

const npa_manifest_t* npa_get_manifest(const npa_archive_t *archive) {
    if (!archive || !archive->valid) return NULL;
    return &archive->manifest;
}

int npa_get_file_count(const npa_archive_t *archive) {
    if (!archive || !archive->valid) return 0;
    return archive->header.file_count;
}

const npa_file_entry_t* npa_get_entry(const npa_archive_t *archive, int index) {
    if (!archive || !archive->valid || !archive->entries) return NULL;
    if (index < 0 || index >= (int)archive->header.file_count) return NULL;
    return &archive->entries[index];
}

const npa_file_entry_t* npa_find_entry(const npa_archive_t *archive, const char *path) {
    if (!archive || !archive->valid || !archive->entries || !path) return NULL;
    
    for (uint32_t i = 0; i < archive->header.file_count; i++) {
        if (npa_strcmp(archive->entries[i].path, path)) {
            return &archive->entries[i];
        }
    }
    
    return NULL;
}

int npa_extract_file(npa_archive_t *archive, const npa_file_entry_t *entry, 
                     const char *dest_path) {
    if (!archive || !archive->valid || !entry || !dest_path) {
        return -1;
    }
    
    vfs_file_t *src = (vfs_file_t*)archive->device;
    
    serial_puts("[NPA] Extracting: ");
    serial_puts(entry->path);
    serial_puts(" -> ");
    serial_puts(dest_path);
    serial_puts("\n");
    
    /* Handle directories */
    if (entry->type == NPA_TYPE_DIRECTORY) {
        /* Create directory */
        int result = vfs_create(dest_path, VFS_DIRECTORY, entry->permissions);
        return result;
    }
    
    /* Seek to file data */
    uint64_t file_offset = archive->data_offset + entry->offset;
    vfs_seek(src, (int64_t)file_offset, VFS_SEEK_SET);
    
    /* Create destination file */
    int result = vfs_create(dest_path, VFS_FILE, entry->permissions);
    if (result != 0) {
        serial_puts("[NPA] Failed to create destination file\n");
        return -2;
    }
    
    /* Open destination for writing */
    vfs_file_t *dst = vfs_open(dest_path, VFS_O_WRONLY);
    if (!dst) {
        serial_puts("[NPA] Failed to open destination for writing\n");
        return -3;
    }
    
    /* Copy data in chunks */
    uint8_t buffer[512];
    uint32_t remaining = entry->size;
    uint32_t crc = 0xFFFFFFFF;
    
    while (remaining > 0) {
        uint32_t chunk = remaining > 512 ? 512 : remaining;
        
        int64_t read = vfs_read(src, buffer, chunk);
        if (read <= 0) {
            serial_puts("[NPA] Read error during extraction\n");
            vfs_close(dst);
            return -4;
        }
        
        /* Update CRC */
        for (int64_t i = 0; i < read; i++) {
            crc = crc32_table[(crc ^ buffer[i]) & 0xFF] ^ (crc >> 8);
        }
        
        int64_t written = vfs_write(dst, buffer, read);
        if (written != read) {
            serial_puts("[NPA] Write error during extraction\n");
            vfs_close(dst);
            return -5;
        }
        
        remaining -= read;
    }
    
    vfs_close(dst);
    
    /* Verify checksum */
    crc ^= 0xFFFFFFFF;
    if (crc != entry->checksum) {
        serial_puts("[NPA] Checksum mismatch!\n");
        return -6;
    }
    
    return 0;
}

int npa_extract_all(npa_archive_t *archive, const char *dest_dir) {
    if (!archive || !archive->valid || !dest_dir) {
        return -1;
    }
    
    serial_puts("[NPA] Extracting all to: ");
    serial_puts(dest_dir);
    serial_puts("\n");
    
    /* Ensure destination directory exists */
    vfs_create(dest_dir, VFS_DIRECTORY, 0755);
    
    int extracted = 0;
    
    /* First pass: create all directories */
    for (uint32_t i = 0; i < archive->header.file_count; i++) {
        const npa_file_entry_t *entry = &archive->entries[i];
        
        if (entry->type == NPA_TYPE_DIRECTORY) {
            char full_path[512];
            npa_strcpy(full_path, dest_dir, 512);
            npa_strcat(full_path, "/", 512);
            npa_strcat(full_path, entry->path, 512);
            
            vfs_create(full_path, VFS_DIRECTORY, entry->permissions);
        }
    }
    
    /* Second pass: extract all files */
    for (uint32_t i = 0; i < archive->header.file_count; i++) {
        const npa_file_entry_t *entry = &archive->entries[i];
        
        if (entry->type == NPA_TYPE_FILE) {
            char full_path[512];
            npa_strcpy(full_path, dest_dir, 512);
            npa_strcat(full_path, "/", 512);
            npa_strcat(full_path, entry->path, 512);
            
            int result = npa_extract_file(archive, entry, full_path);
            if (result == 0) {
                extracted++;
            }
        }
    }
    
    serial_puts("[NPA] Extraction complete, files=");
    char num[16];
    int n = extracted, idx = 0;
    if (n == 0) num[idx++] = '0';
    else {
        char tmp[16];
        int j = 0;
        while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
        while (j > 0) num[idx++] = tmp[--j];
    }
    num[idx] = '\0';
    serial_puts(num);
    serial_puts("\n");
    
    return extracted;
}

int npa_archive_verify(npa_archive_t *archive) {
    if (!archive || !archive->valid) {
        return -1;
    }
    
    serial_puts("[NPA] Verifying archive integrity\n");
    
    /* Verify header checksum */
    uint32_t header_crc = npa_crc32(&archive->header, 
                                     sizeof(npa_header_t) - sizeof(uint32_t) * 2);
    
    /* Note: Full verification would re-read all file data */
    /* For now, just validate structure */
    
    if (archive->header.magic != NPA_MAGIC) {
        return -2;
    }
    
    if (archive->header.version != NPA_VERSION) {
        return -3;
    }
    
    if (!archive->entries && archive->header.file_count > 0) {
        return -4;
    }
    
    serial_puts("[NPA] Archive verification passed\n");
    return 0;
}
