#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H

#include <stdarg.h>
#include <stddef.h>

// Initialize logging system
void klog_init(const char* log_path);

// Simple logging function
void klog(const char* message);

// Formatted logging function (like printf)
void klogf(const char* fmt, ...);

// Close logging system
void klog_close(void);

#endif // KERNEL_LOG_H 