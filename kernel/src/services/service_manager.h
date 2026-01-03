/*
 * NeolyxOS Service Manager
 * 
 * Production-ready service management with:
 * - Service registration and lifecycle
 * - Dependency resolution
 * - Automatic startup ordering
 * - Health monitoring
 * - Service restart on failure
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef SERVICE_MANAGER_H
#define SERVICE_MANAGER_H

#include <stdint.h>

/* ============ Service Limits ============ */
#define MAX_SERVICES        64
#define MAX_DEPENDENCIES    16
#define SERVICE_NAME_LEN    64

/* ============ Service States ============ */
#define SVC_STATE_UNKNOWN       0
#define SVC_STATE_STOPPED       1
#define SVC_STATE_STARTING      2
#define SVC_STATE_RUNNING       3
#define SVC_STATE_STOPPING      4
#define SVC_STATE_FAILED        5
#define SVC_STATE_DISABLED      6

/* ============ Service Types ============ */
#define SVC_TYPE_KERNEL         0   /* Kernel-mode service */
#define SVC_TYPE_DRIVER         1   /* Hardware driver */
#define SVC_TYPE_SYSTEM         2   /* System service */
#define SVC_TYPE_NETWORK        3   /* Network service */
#define SVC_TYPE_STORAGE        4   /* Storage service */
#define SVC_TYPE_USER           5   /* User-mode service */

/* ============ Service Flags ============ */
#define SVC_FLAG_ESSENTIAL      0x0001  /* System cannot run without */
#define SVC_FLAG_AUTO_START     0x0002  /* Start automatically */
#define SVC_FLAG_AUTO_RESTART   0x0004  /* Restart on failure */
#define SVC_FLAG_LAZY           0x0008  /* Start on demand */
#define SVC_FLAG_ONESHOT        0x0010  /* Run once and exit */

/* ============ Callback Types ============ */
typedef int (*service_init_fn)(void *context);
typedef void (*service_cleanup_fn)(void *context);
typedef int (*service_start_fn)(void *context);
typedef int (*service_stop_fn)(void *context);
typedef int (*service_status_fn)(void *context);

/* ============ Service Definition ============ */
typedef struct {
    char                name[SERVICE_NAME_LEN];
    uint8_t             type;
    uint8_t             state;
    uint16_t            flags;
    uint32_t            priority;       /* Lower = starts first */
    
    /* Callbacks */
    service_init_fn     init;
    service_cleanup_fn  cleanup;
    service_start_fn    start;
    service_stop_fn     stop;
    service_status_fn   status;
    
    void               *context;        /* Service-specific data */
    
    /* Dependencies */
    char                depends[MAX_DEPENDENCIES][SERVICE_NAME_LEN];
    int                 depends_count;
    
    /* Status */
    uint32_t            pid;            /* Process ID if applicable */
    uint64_t            start_time;
    uint64_t            uptime;
    uint32_t            restart_count;
    int                 exit_code;
} service_t;

/* ============ Public API ============ */

/**
 * svc_init - Initialize service manager
 * 
 * Returns: 0 on success, negative on error
 */
int svc_init(void);

/**
 * svc_register - Register a service
 * 
 * @name: Service name
 * @type: Service type
 * @init: Initialization callback
 * @start: Start callback
 * @stop: Stop callback
 * @context: Service context
 * 
 * Returns: Service pointer, or NULL on error
 */
service_t *svc_register(const char *name, uint8_t type,
                        service_init_fn init,
                        service_start_fn start,
                        service_stop_fn stop,
                        void *context);

/**
 * svc_unregister - Unregister a service
 */
int svc_unregister(const char *name);

/**
 * svc_add_dependency - Add dependency to service
 */
int svc_add_dependency(service_t *service, const char *depends_on);

/**
 * svc_set_flags - Set service flags
 */
int svc_set_flags(service_t *service, uint16_t flags);

/**
 * svc_start - Start a service and dependencies
 */
int svc_start(const char *name);

/**
 * svc_stop - Stop a service
 */
int svc_stop(const char *name);

/**
 * svc_restart - Restart a service
 */
int svc_restart(const char *name);

/**
 * svc_start_all - Start all auto-start services
 */
int svc_start_all(void);

/**
 * svc_stop_all - Stop all services
 */
int svc_stop_all(void);

/**
 * svc_get_state - Get service state
 */
int svc_get_state(const char *name);

/**
 * svc_find - Find service by name
 */
service_t *svc_find(const char *name);

/**
 * svc_list - Get list of services
 */
int svc_list(service_t **services, int max_count);

/**
 * svc_get_count - Get number of registered services
 */
int svc_get_count(void);

#endif /* SERVICE_MANAGER_H */
