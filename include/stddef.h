#ifndef _NEOLYX_STDDEF_H
#define _NEOLYX_STDDEF_H

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
#if defined(__x86_64__) || defined(_M_X64)
typedef unsigned long long size_t;
#else
typedef unsigned long size_t;
#endif
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#endif // _NEOLYX_STDDEF_H 