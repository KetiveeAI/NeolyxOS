/*
 * NXRT IPC Implementation
 * 
 * Inter-process communication for app-to-system messaging.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "nxrt/nxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#endif

/* ============ Channel State ============ */
#define MAX_CHANNELS 32
#define MAX_HANDLERS 16

typedef struct {
    int             id;
    char            name[64];
    
    /* Callbacks */
    nxrt_message_cb on_message;
    void           *on_message_data;
    
    /* Platform socket */
    int             socket_fd;
    
    /* Message queue */
    nxrt_message_t  queue[64];
    int             queue_head;
    int             queue_tail;
    
    pthread_mutex_t mutex;
    pthread_t       thread;
    int             running;
    
} channel_state_t;

static channel_state_t g_channels[MAX_CHANNELS];
static int g_next_channel_id = 1;

/* ============ Internal Helpers ============ */

static channel_state_t* get_channel(nxrt_channel_t id) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (g_channels[i].id == id) {
            return &g_channels[i];
        }
    }
    return NULL;
}

static void* channel_thread_func(void *arg) {
    channel_state_t *ch = (channel_state_t*)arg;
    
    printf("[NXRT IPC] Channel thread started: %s\n", ch->name);
    
    while (ch->running) {
        pthread_mutex_lock(&ch->mutex);
        
        /* Process queued messages */
        while (ch->queue_head != ch->queue_tail) {
            nxrt_message_t *msg = &ch->queue[ch->queue_head];
            ch->queue_head = (ch->queue_head + 1) % 64;
            
            if (ch->on_message) {
                ch->on_message(ch->on_message_data, msg);
            }
        }
        
        pthread_mutex_unlock(&ch->mutex);
        usleep(1000);  /* 1ms */
    }
    
    printf("[NXRT IPC] Channel thread stopped: %s\n", ch->name);
    return NULL;
}

/* ===========================================================================
 * IPC API
 * =========================================================================*/

nxrt_channel_t nxrt_ipc_create(const char *name) {
    if (!name) return NXRT_INVALID_HANDLE;
    
    /* Find free slot */
    channel_state_t *ch = NULL;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (g_channels[i].id == 0) {
            ch = &g_channels[i];
            break;
        }
    }
    
    if (!ch) return NXRT_INVALID_HANDLE;
    
    memset(ch, 0, sizeof(channel_state_t));
    ch->id = g_next_channel_id++;
    strncpy(ch->name, name, 63);
    ch->socket_fd = -1;
    
    pthread_mutex_init(&ch->mutex, NULL);
    
    /* Start processing thread */
    ch->running = 1;
    pthread_create(&ch->thread, NULL, channel_thread_func, ch);
    
    printf("[NXRT IPC] Created channel: %s (id=%d)\n", name, ch->id);
    
    return ch->id;
}

void nxrt_ipc_destroy(nxrt_channel_t channel_id) {
    channel_state_t *ch = get_channel(channel_id);
    if (!ch) return;
    
    /* Stop thread */
    ch->running = 0;
    pthread_join(ch->thread, NULL);
    
    /* Close socket */
    if (ch->socket_fd >= 0) {
        close(ch->socket_fd);
    }
    
    pthread_mutex_destroy(&ch->mutex);
    
    printf("[NXRT IPC] Destroyed channel: %s\n", ch->name);
    ch->id = 0;
}

nxrt_error_t nxrt_ipc_send(nxrt_channel_t channel_id, const nxrt_message_t *msg) {
    channel_state_t *ch = get_channel(channel_id);
    if (!ch || !msg) return NXRT_ERROR_INVALID;
    
    printf("[NXRT IPC] Send: %s.%s\n", msg->channel, msg->event);
    
    /* In real implementation:
     * - Serialize message
     * - Send over socket/pipe
     */
    
    return NXRT_SUCCESS;
}

nxrt_error_t nxrt_ipc_invoke(nxrt_channel_t channel_id, 
                              const nxrt_message_t *msg,
                              nxrt_message_t *reply,
                              uint32_t timeout_ms) {
    channel_state_t *ch = get_channel(channel_id);
    if (!ch || !msg || !reply) return NXRT_ERROR_INVALID;
    
    printf("[NXRT IPC] Invoke: %s.%s (timeout=%dms)\n", 
           msg->channel, msg->event, timeout_ms);
    
    /* In real implementation:
     * - Send request
     * - Wait for reply with timeout
     */
    
    /* Simulate reply */
    memset(reply, 0, sizeof(nxrt_message_t));
    strncpy(reply->channel, msg->channel, 63);
    strncpy(reply->event, "reply", 63);
    
    return NXRT_SUCCESS;
}

void nxrt_ipc_on_message(nxrt_channel_t channel_id, 
                          nxrt_message_cb callback, 
                          void *userdata) {
    channel_state_t *ch = get_channel(channel_id);
    if (!ch) return;
    
    ch->on_message = callback;
    ch->on_message_data = userdata;
}

/* ===========================================================================
 * System IPC Channels
 * =========================================================================*/

/* Pre-defined system channels */
static nxrt_channel_t g_system_channel = NXRT_INVALID_HANDLE;
static nxrt_channel_t g_app_store_channel = NXRT_INVALID_HANDLE;

nxrt_channel_t nxrt_ipc_system_channel(void) {
    if (g_system_channel == NXRT_INVALID_HANDLE) {
        g_system_channel = nxrt_ipc_create("nxrt.system");
    }
    return g_system_channel;
}

nxrt_channel_t nxrt_ipc_appstore_channel(void) {
    if (g_app_store_channel == NXRT_INVALID_HANDLE) {
        g_app_store_channel = nxrt_ipc_create("nxrt.appstore");
    }
    return g_app_store_channel;
}

/* ===========================================================================
 * Convenience Functions
 * =========================================================================*/

nxrt_error_t nxrt_ipc_emit(const char *channel, 
                            const char *event,
                            const void *data,
                            size_t size) {
    nxrt_channel_t ch = nxrt_ipc_system_channel();
    
    nxrt_message_t msg = {};
    strncpy(msg.channel, channel, 63);
    strncpy(msg.event, event, 63);
    msg.data = (void*)data;
    msg.data_size = size;
    
    return nxrt_ipc_send(ch, &msg);
}

nxrt_error_t nxrt_ipc_call(const char *channel,
                            const char *method,
                            const void *args,
                            size_t args_size,
                            void *result,
                            size_t *result_size,
                            uint32_t timeout_ms) {
    nxrt_channel_t ch = nxrt_ipc_system_channel();
    
    nxrt_message_t msg = {};
    strncpy(msg.channel, channel, 63);
    strncpy(msg.event, method, 63);
    msg.data = (void*)args;
    msg.data_size = args_size;
    
    nxrt_message_t reply = {};
    nxrt_error_t err = nxrt_ipc_invoke(ch, &msg, &reply, timeout_ms);
    
    if (err == NXRT_SUCCESS && result && result_size) {
        size_t copy_size = reply.data_size;
        if (copy_size > *result_size) copy_size = *result_size;
        if (reply.data && copy_size > 0) {
            memcpy(result, reply.data, copy_size);
        }
        *result_size = reply.data_size;
    }
    
    return err;
}
