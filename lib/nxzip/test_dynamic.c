/*
 * NXZIP Dynamic Huffman Test
 * Tests reading ZIP files created by external tools (uses dynamic Huffman)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/nxzip.h"

int main(int argc, char** argv) {
    const char* test_path = argc > 1 ? argv[1] : "/tmp/dynamic_test.zip";
    
    printf("=== NXZIP Dynamic Huffman Test ===\n\n");
    printf("Testing: %s\n\n", test_path);
    
    nxzip_archive_t* archive = nxzip_open(test_path);
    if (!archive) {
        printf("FAIL: Could not open archive\n");
        return 1;
    }
    
    printf("Files in archive: %d\n", nxzip_file_count(archive));
    
    for (int i = 0; i < nxzip_file_count(archive); i++) {
        nxzip_file_info_t info;
        nxzip_get_file_info(archive, i, &info);
        
        printf("  [%d] %s\n", i, info.name);
        printf("      Compressed: %u bytes\n", info.compressed_size);
        printf("      Uncompressed: %u bytes\n", info.uncompressed_size);
        printf("      Method: %s\n", info.method == 8 ? "DEFLATE" : "STORE");
        
        /* Try extraction */
        void* data = NULL;
        size_t size = 0;
        int result = nxzip_extract_to_memory(archive, i, &data, &size);
        
        if (result == NXZIP_OK) {
            printf("      Extraction: PASS (%zu bytes)\n", size);
            printf("      Content preview: \"%.60s%s\"\n", 
                   (char*)data, size > 60 ? "..." : "");
            free(data);
        } else {
            printf("      Extraction: FAIL (error %d)\n", result);
        }
        printf("\n");
    }
    
    nxzip_close(archive);
    
    printf("=== Test Complete ===\n");
    return 0;
}
