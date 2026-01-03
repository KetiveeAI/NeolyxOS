/*
 * NeolyxOS VFS Directory Entry Cache (Dentry)
 * 
 * Path resolution and directory entry caching
 * 
 * Copyright (c) 2024-2025 KetiveeAI and its branches.
 * Licensed under the KetiveeAI License.
 */

#include "vfs.h"
#include "../../drivers/serial.h"

/* ============ Memory Functions ============ */
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern void *memset(void *s, int c, uint64_t n);
extern void *memcpy(void *dest, const void *src, uint64_t n);
extern int strcmp(const char *s1, const char *s2);
extern char *strcpy(char *dest, const char *src);
extern uint64_t strlen(const char *s);

/* ============ Dentry Cache ============ */
#define DENTRY_CACHE_SIZE   512
#define DENTRY_HASH_SIZE    128
#define DENTRY_NAME_MAX     256

typedef struct dentry {
    char                name[DENTRY_NAME_MAX];
    struct inode       *d_inode;
    struct dentry      *d_parent;
    struct dentry      *d_hash_next;
    struct dentry      *d_child;     /* First child */
    struct dentry      *d_sibling;   /* Next sibling */
    struct dentry      *d_lru_next;
    struct dentry      *d_lru_prev;
    int                 d_refcount;
    int                 d_valid;
    int                 d_negative;  /* Negative lookup cache */
} dentry_t;

static dentry_t dentry_cache[DENTRY_CACHE_SIZE];
static dentry_t *dentry_hash[DENTRY_HASH_SIZE];
static dentry_t dentry_lru_head;
static dentry_t *root_dentry = 0;
static int dentry_initialized = 0;

/* ============ String Hash (djb2) ============ */
static uint32_t dentry_hash_name(const char *name, struct dentry *parent)
{
    uint32_t hash = 5381;
    int c;
    
    while ((c = *name++)) {
        hash = ((hash << 5) + hash) + c;
    }
    hash ^= (uint64_t)parent;
    
    return hash % DENTRY_HASH_SIZE;
}

/* ============ LRU Management ============ */
static void dentry_lru_init(void)
{
    dentry_lru_head.d_lru_next = &dentry_lru_head;
    dentry_lru_head.d_lru_prev = &dentry_lru_head;
}

static void dentry_lru_remove(dentry_t *d)
{
    if (d->d_lru_prev && d->d_lru_next) {
        d->d_lru_prev->d_lru_next = d->d_lru_next;
        d->d_lru_next->d_lru_prev = d->d_lru_prev;
        d->d_lru_next = d->d_lru_prev = 0;
    }
}

static void dentry_lru_add_tail(dentry_t *d)
{
    d->d_lru_prev = dentry_lru_head.d_lru_prev;
    d->d_lru_next = &dentry_lru_head;
    dentry_lru_head.d_lru_prev->d_lru_next = d;
    dentry_lru_head.d_lru_prev = d;
}

/* ============ Cache Initialization ============ */
int vfs_dentry_cache_init(void)
{
    if (dentry_initialized) {
        return 0;
    }
    
    memset(dentry_cache, 0, sizeof(dentry_cache));
    memset(dentry_hash, 0, sizeof(dentry_hash));
    dentry_lru_init();
    
    for (int i = 0; i < DENTRY_CACHE_SIZE; i++) {
        dentry_lru_add_tail(&dentry_cache[i]);
    }
    
    dentry_initialized = 1;
    serial_print("[VFS] Dentry cache initialized\r\n");
    return 0;
}

/* ============ Cache Lookup ============ */
static dentry_t *dentry_lookup_hash(const char *name, dentry_t *parent)
{
    uint32_t hash = dentry_hash_name(name, parent);
    dentry_t *d = dentry_hash[hash];
    
    while (d) {
        if (d->d_valid && d->d_parent == parent) {
            if (strcmp(d->name, name) == 0) {
                return d;
            }
        }
        d = d->d_hash_next;
    }
    return 0;
}

/* ============ Cache Insert ============ */
static void dentry_insert_hash(dentry_t *d)
{
    uint32_t hash = dentry_hash_name(d->name, d->d_parent);
    d->d_hash_next = dentry_hash[hash];
    dentry_hash[hash] = d;
}

/* ============ Cache Remove ============ */
static void dentry_remove_hash(dentry_t *d)
{
    uint32_t hash = dentry_hash_name(d->name, d->d_parent);
    dentry_t **pp = &dentry_hash[hash];
    
    while (*pp) {
        if (*pp == d) {
            *pp = d->d_hash_next;
            d->d_hash_next = 0;
            return;
        }
        pp = &(*pp)->d_hash_next;
    }
}

/* ============ Allocate Dentry ============ */
static dentry_t *dentry_alloc(void)
{
    dentry_t *d = dentry_lru_head.d_lru_next;
    
    while (d != &dentry_lru_head) {
        if (d->d_refcount == 0) {
            if (d->d_valid) {
                dentry_remove_hash(d);
                /* Release inode reference */
                if (d->d_inode) {
                    extern void vfs_iput(struct inode *);
                    vfs_iput(d->d_inode);
                }
            }
            dentry_lru_remove(d);
            memset(d, 0, sizeof(dentry_t));
            return d;
        }
        d = d->d_lru_next;
    }
    
    serial_print("[VFS] ERROR: Dentry cache full\r\n");
    return 0;
}

/* ============ Public API ============ */

/**
 * vfs_dentry_create - Create new dentry
 */
dentry_t *vfs_dentry_create(const char *name, dentry_t *parent, struct inode *inode)
{
    if (!dentry_initialized) {
        vfs_dentry_cache_init();
    }
    
    /* Check for existing */
    dentry_t *existing = dentry_lookup_hash(name, parent);
    if (existing) {
        existing->d_refcount++;
        dentry_lru_remove(existing);
        return existing;
    }
    
    dentry_t *d = dentry_alloc();
    if (!d) {
        return 0;
    }
    
    /* Copy name */
    uint64_t len = strlen(name);
    if (len >= DENTRY_NAME_MAX) {
        len = DENTRY_NAME_MAX - 1;
    }
    memcpy(d->name, name, len);
    d->name[len] = '\0';
    
    d->d_inode = inode;
    d->d_parent = parent;
    d->d_refcount = 1;
    d->d_valid = 1;
    d->d_negative = (inode == 0);
    
    /* Link to parent's children */
    if (parent) {
        d->d_sibling = parent->d_child;
        parent->d_child = d;
    }
    
    dentry_insert_hash(d);
    
    return d;
}

/**
 * vfs_dentry_lookup - Find dentry by name in parent
 */
dentry_t *vfs_dentry_lookup(dentry_t *parent, const char *name)
{
    if (!dentry_initialized) {
        vfs_dentry_cache_init();
    }
    
    dentry_t *d = dentry_lookup_hash(name, parent);
    if (d) {
        d->d_refcount++;
        dentry_lru_remove(d);
    }
    return d;
}

/**
 * vfs_dentry_release - Release dentry reference
 */
void vfs_dentry_release(dentry_t *d)
{
    if (!d) {
        return;
    }
    
    if (d->d_refcount > 0) {
        d->d_refcount--;
    }
    
    if (d->d_refcount == 0) {
        dentry_lru_add_tail(d);
    }
}

/**
 * vfs_dentry_invalidate - Invalidate dentry
 */
void vfs_dentry_invalidate(dentry_t *d)
{
    if (!d) {
        return;
    }
    
    dentry_remove_hash(d);
    d->d_valid = 0;
    
    if (d->d_inode) {
        extern void vfs_iput(struct inode *);
        vfs_iput(d->d_inode);
        d->d_inode = 0;
    }
}

/**
 * vfs_dentry_get_root - Get root dentry
 */
dentry_t *vfs_dentry_get_root(void)
{
    if (!root_dentry) {
        /* Create root dentry */
        root_dentry = vfs_dentry_create("/", 0, 0);
        if (root_dentry) {
            root_dentry->d_refcount++;  /* Root never freed */
        }
    }
    return root_dentry;
}

/**
 * vfs_dentry_set_root - Set root dentry inode
 */
void vfs_dentry_set_root(struct inode *root_inode)
{
    dentry_t *d = vfs_dentry_get_root();
    if (d) {
        d->d_inode = root_inode;
        d->d_negative = 0;
    }
}

/* ============ Path Resolution ============ */

/**
 * vfs_path_resolve - Resolve path to dentry
 */
dentry_t *vfs_path_resolve(const char *path)
{
    if (!path || *path == '\0') {
        return 0;
    }
    
    dentry_t *current = vfs_dentry_get_root();
    if (!current) {
        return 0;
    }
    
    /* Handle absolute path */
    if (*path == '/') {
        path++;
    }
    
    if (*path == '\0') {
        current->d_refcount++;
        return current;
    }
    
    char component[DENTRY_NAME_MAX];
    
    while (*path) {
        /* Skip slashes */
        while (*path == '/') {
            path++;
        }
        
        if (*path == '\0') {
            break;
        }
        
        /* Extract component */
        int i = 0;
        while (*path && *path != '/' && i < DENTRY_NAME_MAX - 1) {
            component[i++] = *path++;
        }
        component[i] = '\0';
        
        /* Handle . and .. */
        if (strcmp(component, ".") == 0) {
            continue;
        }
        if (strcmp(component, "..") == 0) {
            if (current->d_parent) {
                current = current->d_parent;
            }
            continue;
        }
        
        /* Lookup in cache first */
        dentry_t *next = vfs_dentry_lookup(current, component);
        
        if (!next) {
            /* Not in cache - try filesystem lookup */
            if (current->d_inode && current->d_inode->i_sb && 
                current->d_inode->i_sb->s_op &&
                current->d_inode->i_sb->s_op->lookup) {
                
                struct inode *child_inode = 0;
                int err = current->d_inode->i_sb->s_op->lookup(
                    current->d_inode, component, &child_inode);
                
                if (err == 0 && child_inode) {
                    next = vfs_dentry_create(component, current, child_inode);
                } else {
                    /* Negative cache entry */
                    next = vfs_dentry_create(component, current, 0);
                }
            }
        }
        
        if (!next || next->d_negative) {
            /* Path component not found */
            if (next) {
                vfs_dentry_release(next);
            }
            return 0;
        }
        
        current = next;
    }
    
    current->d_refcount++;
    return current;
}

/**
 * vfs_dentry_get_inode - Get inode from dentry
 */
struct inode *vfs_dentry_get_inode(dentry_t *d)
{
    if (!d || d->d_negative) {
        return 0;
    }
    return d->d_inode;
}

/**
 * vfs_dentry_get_name - Get dentry name
 */
const char *vfs_dentry_get_name(dentry_t *d)
{
    if (!d) {
        return 0;
    }
    return d->name;
}