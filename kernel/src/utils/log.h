#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

// Logging functions
void klog_init(const char* filename);
void klog(const char* message);
void klogf(const char* fmt, ...);

// Log levels
#define LOG_LEVEL_INFO 0
#define LOG_LEVEL_WARN 1
#define LOG_LEVEL_ERROR 2
#define LOG_LEVEL_DEBUG 3

#endif // LOG_H 