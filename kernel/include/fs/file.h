#ifndef FILE_H
#define FILE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Define ssize_t if not already defined
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef int64_t ssize_t;
#endif

// Neolyx OS Advanced File System (NXFS) - Unique Features

// File types with advanced categorization
typedef enum {
    FILE_TYPE_REGULAR = 0x01,
    FILE_TYPE_DIRECTORY = 0x02,
    FILE_TYPE_SYMLINK = 0x03,
    FILE_TYPE_DEVICE = 0x04,
    FILE_TYPE_PIPE = 0x05,
    FILE_TYPE_SOCKET = 0x06,
    FILE_TYPE_VIRTUAL = 0x07,      // Virtual files (proc, sys)
    FILE_TYPE_ENCRYPTED = 0x08,    // Encrypted files
    FILE_TYPE_COMPRESSED = 0x09,   // Compressed files
    FILE_TYPE_DISTRIBUTED = 0x0A,  // Distributed across multiple nodes
    FILE_TYPE_TEMPORAL = 0x0B,     // Time-based files
    FILE_TYPE_QUANTUM = 0x0C,      // Quantum state files
    FILE_TYPE_HOLOGRAPHIC = 0x0D,  // 3D holographic data
    FILE_TYPE_NEURAL = 0x0E,       // Neural network weights
    FILE_TYPE_GENETIC = 0x0F       // Genetic algorithm data
} nxfs_file_type_t;

// Advanced file permissions with granular control
typedef struct {
    uint32_t owner_read : 1;
    uint32_t owner_write : 1;
    uint32_t owner_execute : 1;
    uint32_t owner_admin : 1;
    uint32_t group_read : 1;
    uint32_t group_write : 1;
    uint32_t group_execute : 1;
    uint32_t group_admin : 1;
    uint32_t other_read : 1;
    uint32_t other_write : 1;
    uint32_t other_execute : 1;
    uint32_t other_admin : 1;
    uint32_t system_read : 1;
    uint32_t system_write : 1;
    uint32_t system_execute : 1;
    uint32_t system_admin : 1;
    uint32_t quantum_access : 1;   // Quantum computing access
    uint32_t neural_access : 1;    // Neural network access
    uint32_t temporal_access : 1;  // Time-based access
    uint32_t holographic_access : 1; // 3D holographic access
    uint32_t genetic_access : 1;   // Genetic algorithm access
    uint32_t distributed_access : 1; // Distributed access
    uint32_t encrypted_access : 1; // Encrypted access
    uint32_t compressed_access : 1; // Compressed access
    uint32_t virtual_access : 1;   // Virtual access
    uint32_t reserved : 8;         // Reserved for future use
} nxfs_permissions_t;

// Multi-dimensional file metadata
typedef struct {
    // Basic information
    uint64_t inode_number;
    nxfs_file_type_t type;
    nxfs_permissions_t permissions;
    uint32_t owner_id;
    uint32_t group_id;
    uint64_t size;
    uint64_t blocks;
    
    // Temporal information
    uint64_t creation_time;
    uint64_t modification_time;
    uint64_t access_time;
    uint64_t deletion_time;        // For temporal files
    uint64_t quantum_time;         // Quantum state time
    
    // Spatial information (3D coordinates)
    int32_t x_coordinate;
    int32_t y_coordinate;
    int32_t z_coordinate;
    uint32_t dimension;            // Multi-dimensional space
    
    // Advanced features
    uint32_t compression_ratio;
    uint32_t encryption_level;
    uint32_t quantum_entanglement; // Quantum entanglement state
    uint32_t neural_connections;   // Neural network connections
    uint32_t genetic_mutations;    // Genetic algorithm mutations
    uint32_t holographic_layers;   // 3D holographic layers
    
    // Distributed information
    uint32_t node_count;           // Number of distributed nodes
    uint32_t replication_factor;
    uint32_t consistency_level;
    
    // Custom metadata
    char custom_metadata[256];
    
    // File signatures and checksums
    uint64_t content_hash;
    uint64_t metadata_hash;
    uint64_t quantum_signature;
    uint64_t neural_signature;
    
    // Access patterns and statistics
    uint64_t access_count;
    uint64_t read_count;
    uint64_t write_count;
    uint64_t quantum_measurements;
    uint64_t neural_activations;
    
    // Future expansion
    uint64_t reserved[16];
} nxfs_file_metadata_t;

// Advanced file handle with multi-dimensional access
typedef struct {
    int fd;                        // File descriptor
    nxfs_file_metadata_t* metadata;
    uint64_t position;             // Current position
    uint64_t quantum_position;     // Quantum state position
    uint64_t neural_position;      // Neural network position
    uint64_t temporal_position;    // Temporal position
    uint64_t holographic_position; // 3D holographic position
    
    // File path
    char path[512];                // File path
    
    // Access modes
    bool read_mode;
    bool write_mode;
    bool quantum_mode;
    bool neural_mode;
    bool temporal_mode;
    bool holographic_mode;
    bool distributed_mode;
    
    // Buffers for different access types
    void* data_buffer;
    void* quantum_buffer;
    void* neural_buffer;
    void* temporal_buffer;
    void* holographic_buffer;
    
    // Buffer sizes
    size_t data_buffer_size;
    size_t quantum_buffer_size;
    size_t neural_buffer_size;
    size_t temporal_buffer_size;
    size_t holographic_buffer_size;
    
    // File system specific data
    void* fs_private_data;
} nxfs_file_handle_t;

// Directory entry with advanced features
typedef struct {
    char name[256];
    nxfs_file_type_t type;
    uint64_t inode;
    uint64_t size;
    uint64_t creation_time;
    uint64_t modification_time;
    
    // Multi-dimensional coordinates
    int32_t x, y, z;
    uint32_t dimension;
    
    // Advanced attributes
    uint32_t quantum_state;
    uint32_t neural_state;
    uint32_t temporal_state;
    uint32_t holographic_state;
    
    // Custom attributes
    char attributes[128];
} nxfs_dirent_t;

// File system operations
typedef struct {
    // Standard operations
    int (*open)(const char* path, int flags, nxfs_file_handle_t* handle);
    int (*close)(nxfs_file_handle_t* handle);
    ssize_t (*read)(nxfs_file_handle_t* handle, void* buffer, size_t size);
    ssize_t (*write)(nxfs_file_handle_t* handle, const void* buffer, size_t size);
    int (*seek)(nxfs_file_handle_t* handle, int64_t offset, int whence);
    
    // Advanced operations
    int (*quantum_read)(nxfs_file_handle_t* handle, void* buffer, size_t size);
    int (*quantum_write)(nxfs_file_handle_t* handle, const void* buffer, size_t size);
    int (*neural_read)(nxfs_file_handle_t* handle, void* buffer, size_t size);
    int (*neural_write)(nxfs_file_handle_t* handle, const void* buffer, size_t size);
    int (*temporal_read)(nxfs_file_handle_t* handle, void* buffer, size_t size, uint64_t time);
    int (*temporal_write)(nxfs_file_handle_t* handle, const void* buffer, size_t size, uint64_t time);
    int (*holographic_read)(nxfs_file_handle_t* handle, void* buffer, size_t size, int32_t x, int32_t y, int32_t z);
    int (*holographic_write)(nxfs_file_handle_t* handle, const void* buffer, size_t size, int32_t x, int32_t y, int32_t z);
    
    // Directory operations
    int (*opendir)(const char* path, nxfs_file_handle_t* handle);
    int (*readdir)(nxfs_file_handle_t* handle, nxfs_dirent_t* entry);
    int (*closedir)(nxfs_file_handle_t* handle);
    
    // Metadata operations
    int (*get_metadata)(const char* path, nxfs_file_metadata_t* metadata);
    int (*set_metadata)(const char* path, const nxfs_file_metadata_t* metadata);
    
    // Advanced file system operations
    int (*create_quantum_file)(const char* path, uint32_t quantum_bits);
    int (*create_neural_file)(const char* path, uint32_t neural_layers);
    int (*create_temporal_file)(const char* path, uint64_t time_span);
    int (*create_holographic_file)(const char* path, uint32_t width, uint32_t height, uint32_t depth);
    int (*create_distributed_file)(const char* path, uint32_t node_count);
    
    // File system management
    int (*mount)(const char* device, const char* mount_point);
    int (*unmount)(const char* mount_point);
    int (*format)(const char* device, const char* fs_type);
} nxfs_operations_t;

// File system types
typedef enum {
    NXFS_TYPE_NXFS = 0x01,         // Neolyx File System
    NXFS_TYPE_QUANTUM = 0x02,      // Quantum File System
    NXFS_TYPE_NEURAL = 0x03,       // Neural File System
    NXFS_TYPE_TEMPORAL = 0x04,     // Temporal File System
    NXFS_TYPE_HOLOGRAPHIC = 0x05,  // Holographic File System
    NXFS_TYPE_DISTRIBUTED = 0x06,  // Distributed File System
    NXFS_TYPE_VIRTUAL = 0x07,      // Virtual File System
    NXFS_TYPE_ENCRYPTED = 0x08,    // Encrypted File System
    NXFS_TYPE_COMPRESSED = 0x09,   // Compressed File System
    NXFS_TYPE_GENETIC = 0x0A       // Genetic File System
} nxfs_type_t;

// File system mount information
typedef struct {
    char device[64];
    char mount_point[256];
    nxfs_type_t type;
    nxfs_operations_t* ops;
    void* private_data;
    bool mounted;
    uint64_t mount_time;
} nxfs_mount_info_t;

// Function declarations
int nxfs_init(void);
void nxfs_deinit(void);

// File operations
int nxfs_open(const char* path, int flags, nxfs_file_handle_t* handle);
int nxfs_close(nxfs_file_handle_t* handle);
ssize_t nxfs_read(nxfs_file_handle_t* handle, void* buffer, size_t size);
ssize_t nxfs_write(nxfs_file_handle_t* handle, const void* buffer, size_t size);
int nxfs_seek(nxfs_file_handle_t* handle, int64_t offset, int whence);

// Advanced file operations
int nxfs_quantum_read(nxfs_file_handle_t* handle, void* buffer, size_t size);
int nxfs_quantum_write(nxfs_file_handle_t* handle, const void* buffer, size_t size);
int nxfs_neural_read(nxfs_file_handle_t* handle, void* buffer, size_t size);
int nxfs_neural_write(nxfs_file_handle_t* handle, const void* buffer, size_t size);
int nxfs_temporal_read(nxfs_file_handle_t* handle, void* buffer, size_t size, uint64_t time);
int nxfs_temporal_write(nxfs_file_handle_t* handle, const void* buffer, size_t size, uint64_t time);
int nxfs_holographic_read(nxfs_file_handle_t* handle, void* buffer, size_t size, int32_t x, int32_t y, int32_t z);
int nxfs_holographic_write(nxfs_file_handle_t* handle, const void* buffer, size_t size, int32_t x, int32_t y, int32_t z);

// Directory operations
int nxfs_opendir(const char* path, nxfs_file_handle_t* handle);
int nxfs_readdir(nxfs_file_handle_t* handle, nxfs_dirent_t* entry);
int nxfs_closedir(nxfs_file_handle_t* handle);

// Metadata operations
int nxfs_get_metadata(const char* path, nxfs_file_metadata_t* metadata);
int nxfs_set_metadata(const char* path, const nxfs_file_metadata_t* metadata);

// Advanced file creation
int nxfs_create_quantum_file(const char* path, uint32_t quantum_bits);
int nxfs_create_neural_file(const char* path, uint32_t neural_layers);
int nxfs_create_temporal_file(const char* path, uint64_t time_span);
int nxfs_create_holographic_file(const char* path, uint32_t width, uint32_t height, uint32_t depth);
int nxfs_create_distributed_file(const char* path, uint32_t node_count);

// File system management
int nxfs_mount(const char* device, const char* mount_point, nxfs_type_t type);
int nxfs_unmount(const char* mount_point);
int nxfs_format(const char* device, nxfs_type_t type);

// Utility functions
uint64_t nxfs_get_time(void);
uint64_t nxfs_calculate_hash(const void* data, size_t size);
int nxfs_validate_path(const char* path);

#endif // FILE_H 