/*
 * NXAudio Metadata Extractors Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============ Helpers ============ */

static uint32_t read_syncsafe(const uint8_t *p) {
    return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) |
           ((p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

static uint32_t read_be32(const uint8_t *p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static uint32_t read_le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static void copy_string(char *dest, size_t dest_size, const char *src, size_t src_len) {
    size_t len = (src_len < dest_size - 1) ? src_len : dest_size - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
    
    /* Trim trailing whitespace */
    while (len > 0 && (dest[len-1] == ' ' || dest[len-1] == '\0')) {
        dest[--len] = '\0';
    }
}

/* ============ ID3v1 Parser ============ */

int nx_metadata_parse_id3v1(const void *data, size_t size, nx_metadata_t *meta) {
    if (!data || !meta || size < 128) return -1;
    
    const uint8_t *p = (const uint8_t*)data + size - 128;
    
    if (p[0] != 'T' || p[1] != 'A' || p[2] != 'G') {
        return -1;  /* No ID3v1 tag */
    }
    
    copy_string(meta->title, sizeof(meta->title), (char*)p + 3, 30);
    copy_string(meta->artist, sizeof(meta->artist), (char*)p + 33, 30);
    copy_string(meta->album, sizeof(meta->album), (char*)p + 63, 30);
    
    char year_str[5] = {0};
    memcpy(year_str, p + 93, 4);
    meta->year = atoi(year_str);
    
    /* ID3v1.1 track number */
    if (p[125] == 0 && p[126] != 0) {
        meta->track_number = p[126];
        copy_string(meta->comment, sizeof(meta->comment), (char*)p + 97, 28);
    } else {
        copy_string(meta->comment, sizeof(meta->comment), (char*)p + 97, 30);
    }
    
    return 0;
}

/* ============ ID3v2 Parser ============ */

int nx_metadata_parse_id3v2(const void *data, size_t size, nx_metadata_t *meta) {
    if (!data || !meta || size < 10) return -1;
    
    const uint8_t *p = (const uint8_t*)data;
    
    if (p[0] != 'I' || p[1] != 'D' || p[2] != '3') {
        return -1;
    }
    
    uint8_t version = p[3];
    /* uint8_t revision = p[4]; */
    uint8_t flags = p[5];
    uint32_t tag_size = read_syncsafe(p + 6);
    
    (void)flags;  /* Extended header, etc. */
    
    const uint8_t *end = p + 10 + tag_size;
    p += 10;
    
    /* Parse frames */
    while (p + 10 < end && p < (uint8_t*)data + size) {
        char frame_id[5] = {0};
        uint32_t frame_size;
        
        if (version >= 3) {
            memcpy(frame_id, p, 4);
            if (version >= 4) {
                frame_size = read_syncsafe(p + 4);
            } else {
                frame_size = read_be32(p + 4);
            }
            p += 10;
        } else {
            /* ID3v2.2 */
            memcpy(frame_id, p, 3);
            frame_size = (p[3] << 16) | (p[4] << 8) | p[5];
            p += 6;
        }
        
        if (frame_size == 0 || frame_id[0] == 0) break;
        if (p + frame_size > end) break;
        
        /* Text encoding byte */
        int encoding = p[0];
        const char *text = (const char*)p + 1;
        size_t text_len = frame_size - 1;
        
        /* Skip BOM for UTF-16 */
        if (encoding == 1 || encoding == 2) {
            if (text_len >= 2 && ((uint8_t)text[0] == 0xFF || (uint8_t)text[0] == 0xFE)) {
                text += 2;
                text_len -= 2;
            }
            /* TODO: Proper UTF-16 to UTF-8 conversion */
        }
        
        if (strcmp(frame_id, "TIT2") == 0 || strcmp(frame_id, "TT2") == 0) {
            copy_string(meta->title, sizeof(meta->title), text, text_len);
        }
        else if (strcmp(frame_id, "TPE1") == 0 || strcmp(frame_id, "TP1") == 0) {
            copy_string(meta->artist, sizeof(meta->artist), text, text_len);
        }
        else if (strcmp(frame_id, "TALB") == 0 || strcmp(frame_id, "TAL") == 0) {
            copy_string(meta->album, sizeof(meta->album), text, text_len);
        }
        else if (strcmp(frame_id, "TYER") == 0 || strcmp(frame_id, "TYE") == 0 ||
                 strcmp(frame_id, "TDRC") == 0) {
            char year[8] = {0};
            copy_string(year, sizeof(year), text, text_len > 4 ? 4 : text_len);
            meta->year = atoi(year);
        }
        else if (strcmp(frame_id, "TRCK") == 0 || strcmp(frame_id, "TRK") == 0) {
            meta->track_number = atoi(text);
        }
        else if (strcmp(frame_id, "TCON") == 0 || strcmp(frame_id, "TCO") == 0) {
            copy_string(meta->genre, sizeof(meta->genre), text, text_len);
        }
        else if (strcmp(frame_id, "APIC") == 0 || strcmp(frame_id, "PIC") == 0) {
            /* Album art - store pointer */
            meta->cover_art = (uint8_t*)p;
            meta->cover_art_size = frame_size;
        }
        
        p += frame_size;
    }
    
    return 0;
}

/* ============ Vorbis Comments Parser ============ */

int nx_metadata_parse_vorbis(const void *data, size_t size, nx_metadata_t *meta) {
    if (!data || !meta || size < 8) return -1;
    
    const uint8_t *p = (const uint8_t*)data;
    
    /* Vendor string length */
    uint32_t vendor_len = read_le32(p);
    p += 4 + vendor_len;
    
    if (p + 4 > (uint8_t*)data + size) return -1;
    
    /* Number of comments */
    uint32_t num_comments = read_le32(p);
    p += 4;
    
    for (uint32_t i = 0; i < num_comments && p + 4 < (uint8_t*)data + size; i++) {
        uint32_t comment_len = read_le32(p);
        p += 4;
        
        if (p + comment_len > (uint8_t*)data + size) break;
        
        /* Find = separator */
        char *eq = memchr(p, '=', comment_len);
        if (!eq) {
            p += comment_len;
            continue;
        }
        
        size_t key_len = eq - (char*)p;
        const char *value = eq + 1;
        size_t value_len = comment_len - key_len - 1;
        
        if (strncasecmp((char*)p, "TITLE", key_len) == 0) {
            copy_string(meta->title, sizeof(meta->title), value, value_len);
        }
        else if (strncasecmp((char*)p, "ARTIST", key_len) == 0) {
            copy_string(meta->artist, sizeof(meta->artist), value, value_len);
        }
        else if (strncasecmp((char*)p, "ALBUM", key_len) == 0) {
            copy_string(meta->album, sizeof(meta->album), value, value_len);
        }
        else if (strncasecmp((char*)p, "DATE", key_len) == 0 ||
                 strncasecmp((char*)p, "YEAR", key_len) == 0) {
            copy_string(meta->date, sizeof(meta->date), value, value_len);
            meta->year = atoi(value);
        }
        else if (strncasecmp((char*)p, "TRACKNUMBER", key_len) == 0) {
            meta->track_number = atoi(value);
        }
        else if (strncasecmp((char*)p, "GENRE", key_len) == 0) {
            copy_string(meta->genre, sizeof(meta->genre), value, value_len);
        }
        
        p += comment_len;
    }
    
    return 0;
}

/* ============ Public API ============ */

int nx_metadata_load(const char *filepath, nx_metadata_t *meta) {
    if (!filepath || !meta) return -1;
    
    nx_metadata_clear(meta);
    
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0 || size > 50 * 1024 * 1024) {  /* Max 50MB */
        fclose(f);
        return -1;
    }
    
    /* Read only first 256KB for metadata */
    size_t read_size = (size > 256 * 1024) ? 256 * 1024 : (size_t)size;
    void *data = malloc(read_size + 128);  /* Extra for ID3v1 at end */
    if (!data) {
        fclose(f);
        return -1;
    }
    
    fread(data, 1, read_size, f);
    
    /* Read ID3v1 from end */
    if (size >= 128) {
        fseek(f, -128, SEEK_END);
        fread((uint8_t*)data + read_size, 1, 128, f);
    }
    
    fclose(f);
    
    int result = nx_metadata_parse(data, read_size, meta);
    
    /* Try ID3v1 if no metadata found */
    if (meta->title[0] == '\0' && size >= 128) {
        nx_metadata_parse_id3v1(data, read_size + 128, meta);
    }
    
    free(data);
    
    return result;
}

int nx_metadata_parse(const void *data, size_t size, nx_metadata_t *meta) {
    if (!data || !meta || size < 10) return -1;
    
    const uint8_t *p = (const uint8_t*)data;
    
    /* Try ID3v2 */
    if (p[0] == 'I' && p[1] == 'D' && p[2] == '3') {
        return nx_metadata_parse_id3v2(data, size, meta);
    }
    
    /* Try OGG (Vorbis comments in OGG container) */
    if (p[0] == 'O' && p[1] == 'g' && p[2] == 'g' && p[3] == 'S') {
        /* Find Vorbis comment packet */
        /* Simplified: skip to second OGG page */
        size_t offset = 0;
        int page = 0;
        while (offset + 27 < size && page < 5) {
            if (p[offset] == 'O' && p[offset+1] == 'g' && p[offset+2] == 'g') {
                uint8_t segments = p[offset + 26];
                size_t page_size = 27 + segments;
                for (int s = 0; s < segments; s++) {
                    page_size += p[offset + 27 + s];
                }
                
                if (page == 1) {
                    /* Second page should have Vorbis comment */
                    const uint8_t *payload = p + offset + 27 + segments;
                    if (payload[0] == 3 && memcmp(payload + 1, "vorbis", 6) == 0) {
                        return nx_metadata_parse_vorbis(payload + 7, size - (payload + 7 - p), meta);
                    }
                }
                
                offset += page_size;
                page++;
            } else {
                offset++;
            }
        }
    }
    
    /* Try FLAC (metadata blocks) */
    if (p[0] == 'f' && p[1] == 'L' && p[2] == 'a' && p[3] == 'C') {
        size_t offset = 4;
        while (offset + 4 < size) {
            int last = (p[offset] >> 7) & 1;
            int block_type = p[offset] & 0x7F;
            uint32_t block_size = (p[offset+1] << 16) | (p[offset+2] << 8) | p[offset+3];
            
            if (block_type == 4) {  /* VORBIS_COMMENT */
                return nx_metadata_parse_vorbis(p + offset + 4, block_size, meta);
            }
            
            offset += 4 + block_size;
            if (last) break;
        }
    }
    
    return -1;
}

void nx_metadata_clear(nx_metadata_t *meta) {
    if (meta) {
        memset(meta, 0, sizeof(*meta));
    }
}
