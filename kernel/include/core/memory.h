#ifndef _NEOLYX_MEMORY_H
#define _NEOLYX_MEMORY_H

#include <stddef.h>

void* memory_alloc(size_t size);
void memory_free(void* ptr);

#endif // _NEOLYX_MEMORY_H 