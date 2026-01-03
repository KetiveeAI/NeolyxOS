#ifndef _NEOLYX_STDINT_H
#define _NEOLYX_STDINT_H

/* Sized integer types */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

/* Pointer-sized types */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long long size_t;
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long long ssize_t;
#endif

typedef unsigned long long uintptr_t;
typedef long long intptr_t;

/* Boolean */
#ifndef __bool_true_false_are_defined
#define __bool_true_false_are_defined 1
typedef int bool;
#define true 1
#define false 0
#endif

/* NULL */
#ifndef NULL
#define NULL ((void*)0)
#endif

#endif /* _NEOLYX_STDINT_H */
