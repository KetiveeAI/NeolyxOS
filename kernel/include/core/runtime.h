#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>
#include <stddef.h>

// Runtime types
#define RUNTIME_CSHARP 1
#define RUNTIME_CPP    2
#define RUNTIME_OBJC   3
#define RUNTIME_RUST   4

// Runtime initialization and cleanup
void runtime_init(void);
void runtime_cleanup(void);

// Generic runtime functions
uint64_t runtime_load(uint32_t runtime_type, const char* path);
uint64_t runtime_exec(uint32_t runtime_type, const char* name, void* args);

// C# Runtime functions
void csharp_runtime_init(void* runtime);
uint64_t runtime_csharp_load(const char* assembly_path);
uint64_t runtime_csharp_exec(const char* method_name, void* args);

// C++ Runtime functions
void cpp_runtime_init(void* runtime);
uint64_t runtime_cpp_load(const char* library_path);
uint64_t runtime_cpp_exec(const char* function_name, void* args);

// Objective-C Runtime functions
void objc_runtime_init(void* runtime);
uint64_t runtime_objc_load(const char* bundle_path);
uint64_t runtime_objc_exec(const char* selector_name, void* args);

// Rust Runtime functions
void rust_runtime_init(void* runtime);
uint64_t runtime_rust_load(const char* library_path);
uint64_t runtime_rust_exec(const char* function_name, void* args);

// Runtime structures (forward declarations)
typedef struct csharp_runtime csharp_runtime_t;
typedef struct cpp_runtime cpp_runtime_t;
typedef struct objc_runtime objc_runtime_t;
typedef struct rust_runtime rust_runtime_t;

#endif // RUNTIME_H 