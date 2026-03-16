/*
 * NXZIP Test Program
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/nxzip.h"

int main(void) {
    printf("=== NXZIP Test Suite ===\n\n");
    
    /* Test 1: CRC32 */
    printf("Test 1: CRC32\n");
    const char* test_data = "Hello, NeolyxOS!";
    uint32_t crc = nxzip_crc32(test_data, strlen(test_data));
    printf("  CRC32 of '%s': 0x%08X\n", test_data, crc);
    printf("  %s\n\n", crc != 0 ? "PASS" : "FAIL");
    
    /* Test 2: Compress and decompress */
    printf("Test 2: DEFLATE compress/decompress\n");
    const char* original = "This is a test string that should compress well. "
                          "This is a test string that should compress well. "
                          "This is a test string that should compress well.";
    size_t orig_len = strlen(original);
    
    void* compressed = NULL;
    size_t comp_size = 0;
    
    int result = nxzip_deflate(original, orig_len, &compressed, &comp_size);
    if (result == NXZIP_OK) {
        printf("  Original size: %zu bytes\n", orig_len);
        printf("  Compressed size: %zu bytes\n", comp_size);
        printf("  Ratio: %.1f%%\n", 100.0 * comp_size / orig_len);
        
        /* Decompress */
        char* decompressed = malloc(orig_len + 1);
        result = nxzip_inflate(compressed, comp_size, decompressed, orig_len);
        decompressed[orig_len] = '\0';
        
        if (result == NXZIP_OK && strcmp(original, decompressed) == 0) {
            printf("  Decompression: PASS\n");
        } else {
            printf("  Decompression: FAIL\n");
        }
        
        free(compressed);
        free(decompressed);
    } else {
        printf("  Compression: FAIL (%d)\n", result);
    }
    
    /* Test 3: Create and read archive */
    printf("\nTest 3: Archive create/read\n");
    
    nxzip_archive_t* archive = nxzip_create("/tmp/test.nxzip");
    if (archive) {
        nxzip_add_data(archive, "File 1 content", 14, "file1.txt", NXZIP_STORE);
        nxzip_add_data(archive, "File 2 content that is longer", 29, "file2.txt", NXZIP_DEFLATE);
        nxzip_add_data(archive, original, orig_len, "repeated.txt", NXZIP_DEFLATE);
        nxzip_finalize(archive);
        
        printf("  Created: /tmp/test.nxzip\n");
        
        /* Read it back */
        archive = nxzip_open("/tmp/test.nxzip");
        if (archive) {
            printf("  Files in archive: %d\n", nxzip_file_count(archive));
            
            for (int i = 0; i < nxzip_file_count(archive); i++) {
                nxzip_file_info_t info;
                nxzip_get_file_info(archive, i, &info);
                printf("    %d: %s (%u bytes)\n", i, info.name, info.uncompressed_size);
            }
            
            /* Extract to memory */
            void* data = NULL;
            size_t size = 0;
            if (nxzip_extract_to_memory(archive, 0, &data, &size) == NXZIP_OK) {
                printf("  Extracted file1.txt: '%.*s'\n", (int)size, (char*)data);
                free(data);
            }
            
            nxzip_close(archive);
            printf("  Archive test: PASS\n");
        } else {
            printf("  Read archive: FAIL\n");
        }
    } else {
        printf("  Create archive: FAIL\n");
    }
    
    printf("\n=== Tests Complete ===\n");
    return 0;
}
