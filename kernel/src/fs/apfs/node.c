#include <fs/apfs/apfs.h>
#include <fs/vfs.h>
#include <mm/kmalloc.h>

#define APFS_BTREE_NODE_FLAG_LEAF 0x0001

typedef struct {
    apfs_obj_header_t btn_o;
    uint16_t btn_flags;
    uint16_t btn_level;
    uint32_t btn_nkeys;
    // ... followed by key/pointer pairs
} apfs_btree_node_t;

int apfs_btree_search(apfs_fs_t *fs, uint64_t oid, uint64_t xid, 
                     const void *key, size_t key_len, void *val)
{
    // Read root node
    apfs_btree_node_t *node;
    if (apfs_read_object(fs, oid, xid, (void**)&node) != 0)
        return -EIO;
    
    while (1) {
        // Binary search within node
        int low = 0;
        int high = node->btn_nkeys - 1;
        int found = -1;
        
        while (low <= high) {
            int mid = (low + high) / 2;
            void *node_key = (char*)node + sizeof(apfs_btree_node_t) + 
                           (mid * fs->key_compare_info.key_len);
            
            int cmp = fs->key_compare(key, node_key);
            if (cmp == 0) {
                found = mid;
                break;
            } else if (cmp < 0) {
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        }
        
        if (node->btn_flags & APFS_BTREE_NODE_FLAG_LEAF) {
            // Handle leaf node
            if (found >= 0) {
                // Key found
                void *value = (char*)node + fs->val_offset_func(node, found);
                memcpy(val, value, fs->val_size);
                kfree(node);
                return 0;
            }
            kfree(node);
            return -ENOENT;
        } else {
            // Handle internal node
            uint64_t child_oid;
            if (found >= 0) {
                child_oid = fs->pointer_extract_func(node, found + 1);
            } else {
                child_oid = fs->pointer_extract_func(node, low);
            }
            
            kfree(node);
            if (apfs_read_object(fs, child_oid, xid, (void**)&node) != 0)
                return -EIO;
        }
    }
} 