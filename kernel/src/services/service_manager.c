/*
 * NeolyxOS Service Manager Implementation
 * 
 * Service lifecycle and dependency management
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "service_manager.h"
#include "../drivers/serial.h"

/* ============ Memory Functions ============ */
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* ============ String Functions ============ */
static int str_len(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void str_copy(char *dest, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/* ============ Service Storage ============ */
static service_t services[MAX_SERVICES];
static int service_count = 0;
static int initialized = 0;

/* ============ Helper Functions ============ */

static service_t *find_free_slot(void) {
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (services[i].state == SVC_STATE_UNKNOWN && services[i].name[0] == '\0') {
            return &services[i];
        }
    }
    return 0;
}

static void print_service_log(const char *action, const char *name) {
    serial_puts("[SVC] ");
    serial_puts(action);
    serial_puts(": ");
    serial_puts(name);
    serial_puts("\r\n");
}

/* ============ Dependency Resolution ============ */

static int start_dependencies(service_t *svc) {
    for (int i = 0; i < svc->depends_count; i++) {
        service_t *dep = svc_find(svc->depends[i]);
        if (!dep) {
            serial_puts("[SVC] Missing dependency: ");
            serial_puts(svc->depends[i]);
            serial_puts("\r\n");
            return -1;
        }
        
        if (dep->state != SVC_STATE_RUNNING) {
            int result = svc_start(dep->name);
            if (result != 0) {
                serial_puts("[SVC] Failed to start dependency: ");
                serial_puts(dep->name);
                serial_puts("\r\n");
                return -1;
            }
        }
    }
    return 0;
}

/* ============ Public API ============ */

int svc_init(void) {
    serial_puts("[SVC] Initializing service manager...\r\n");
    
    /* Clear all services */
    for (int i = 0; i < MAX_SERVICES; i++) {
        services[i].name[0] = '\0';
        services[i].state = SVC_STATE_UNKNOWN;
        services[i].type = 0;
        services[i].flags = 0;
        services[i].priority = 100;
        services[i].init = 0;
        services[i].cleanup = 0;
        services[i].start = 0;
        services[i].stop = 0;
        services[i].status = 0;
        services[i].context = 0;
        services[i].depends_count = 0;
        services[i].pid = 0;
        services[i].start_time = 0;
        services[i].uptime = 0;
        services[i].restart_count = 0;
        services[i].exit_code = 0;
    }
    
    service_count = 0;
    initialized = 1;
    
    serial_puts("[SVC] Service manager initialized\r\n");
    return 0;
}

service_t *svc_register(const char *name, uint8_t type,
                        service_init_fn init,
                        service_start_fn start,
                        service_stop_fn stop,
                        void *context) {
    if (!initialized || !name) {
        return 0;
    }
    
    /* Check if already exists */
    if (svc_find(name)) {
        serial_puts("[SVC] Service already exists: ");
        serial_puts(name);
        serial_puts("\r\n");
        return 0;
    }
    
    /* Find free slot */
    service_t *svc = find_free_slot();
    if (!svc) {
        serial_puts("[SVC] No free service slots\r\n");
        return 0;
    }
    
    /* Initialize service */
    str_copy(svc->name, name, SERVICE_NAME_LEN);
    svc->type = type;
    svc->state = SVC_STATE_STOPPED;
    svc->flags = 0;
    svc->priority = 100;
    svc->init = init;
    svc->start = start;
    svc->stop = stop;
    svc->context = context;
    svc->depends_count = 0;
    
    service_count++;
    
    print_service_log("Registered", name);
    
    /* Call init callback if provided */
    if (init) {
        if (init(context) != 0) {
            serial_puts("[SVC] Init callback failed for: ");
            serial_puts(name);
            serial_puts("\r\n");
            svc->state = SVC_STATE_FAILED;
            return svc;
        }
    }
    
    return svc;
}

int svc_unregister(const char *name) {
    if (!initialized || !name) return -1;
    
    service_t *svc = svc_find(name);
    if (!svc) return -1;
    
    /* Stop if running */
    if (svc->state == SVC_STATE_RUNNING) {
        svc_stop(name);
    }
    
    /* Call cleanup */
    if (svc->cleanup) {
        svc->cleanup(svc->context);
    }
    
    /* Clear slot */
    svc->name[0] = '\0';
    svc->state = SVC_STATE_UNKNOWN;
    service_count--;
    
    print_service_log("Unregistered", name);
    return 0;
}

int svc_add_dependency(service_t *service, const char *depends_on) {
    if (!service || !depends_on) return -1;
    
    if (service->depends_count >= MAX_DEPENDENCIES) {
        return -2;
    }
    
    str_copy(service->depends[service->depends_count], depends_on, SERVICE_NAME_LEN);
    service->depends_count++;
    
    return 0;
}

int svc_set_flags(service_t *service, uint16_t flags) {
    if (!service) return -1;
    service->flags = flags;
    return 0;
}

int svc_start(const char *name) {
    if (!initialized || !name) return -1;
    
    service_t *svc = svc_find(name);
    if (!svc) {
        serial_puts("[SVC] Service not found: ");
        serial_puts(name);
        serial_puts("\r\n");
        return -1;
    }
    
    if (svc->state == SVC_STATE_RUNNING) {
        return 0; /* Already running */
    }
    
    if (svc->state == SVC_STATE_DISABLED) {
        return -2; /* Disabled */
    }
    
    print_service_log("Starting", name);
    svc->state = SVC_STATE_STARTING;
    
    /* Start dependencies first */
    if (start_dependencies(svc) != 0) {
        svc->state = SVC_STATE_FAILED;
        return -3;
    }
    
    /* Call start callback */
    if (svc->start) {
        int result = svc->start(svc->context);
        if (result != 0) {
            svc->state = SVC_STATE_FAILED;
            svc->exit_code = result;
            print_service_log("Failed to start", name);
            return -4;
        }
    }
    
    svc->state = SVC_STATE_RUNNING;
    svc->restart_count = 0;
    
    print_service_log("Started", name);
    return 0;
}

int svc_stop(const char *name) {
    if (!initialized || !name) return -1;
    
    service_t *svc = svc_find(name);
    if (!svc) return -1;
    
    if (svc->state != SVC_STATE_RUNNING) {
        return 0; /* Not running */
    }
    
    print_service_log("Stopping", name);
    svc->state = SVC_STATE_STOPPING;
    
    /* Call stop callback */
    if (svc->stop) {
        svc->stop(svc->context);
    }
    
    svc->state = SVC_STATE_STOPPED;
    print_service_log("Stopped", name);
    return 0;
}

int svc_restart(const char *name) {
    if (!initialized || !name) return -1;
    
    svc_stop(name);
    return svc_start(name);
}

int svc_start_all(void) {
    if (!initialized) return -1;
    
    serial_puts("[SVC] Starting all auto-start services...\r\n");
    
    int started = 0;
    
    /* Start by priority (lower first) */
    for (int priority = 0; priority < 256; priority++) {
        for (int i = 0; i < MAX_SERVICES; i++) {
            service_t *svc = &services[i];
            
            if (svc->name[0] == '\0') continue;
            if (svc->state == SVC_STATE_RUNNING) continue;
            if (!(svc->flags & SVC_FLAG_AUTO_START)) continue;
            if (svc->priority != priority) continue;
            
            if (svc_start(svc->name) == 0) {
                started++;
            }
        }
    }
    
    serial_puts("[SVC] Started ");
    char buf[4];
    buf[0] = '0' + (started / 10);
    buf[1] = '0' + (started % 10);
    buf[2] = '\0';
    serial_puts(buf);
    serial_puts(" services\r\n");
    
    return started;
}

int svc_stop_all(void) {
    if (!initialized) return -1;
    
    serial_puts("[SVC] Stopping all services...\r\n");
    
    int stopped = 0;
    
    /* Stop in reverse priority order */
    for (int priority = 255; priority >= 0; priority--) {
        for (int i = 0; i < MAX_SERVICES; i++) {
            service_t *svc = &services[i];
            
            if (svc->name[0] == '\0') continue;
            if (svc->state != SVC_STATE_RUNNING) continue;
            if (svc->priority != priority) continue;
            
            svc_stop(svc->name);
            stopped++;
        }
    }
    
    return stopped;
}

int svc_get_state(const char *name) {
    service_t *svc = svc_find(name);
    if (!svc) return -1;
    return svc->state;
}

service_t *svc_find(const char *name) {
    if (!name) return 0;
    
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (services[i].name[0] && str_cmp(services[i].name, name) == 0) {
            return &services[i];
        }
    }
    return 0;
}

int svc_list(service_t **out_services, int max_count) {
    if (!out_services) return -1;
    
    int count = 0;
    for (int i = 0; i < MAX_SERVICES && count < max_count; i++) {
        if (services[i].name[0]) {
            out_services[count++] = &services[i];
        }
    }
    return count;
}

int svc_get_count(void) {
    return service_count;
}
