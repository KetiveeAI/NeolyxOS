/*
 * NeolyxOS Clock Application - NTP Client
 * 
 * Implements NTP (Network Time Protocol) client using UDP.
 * Connects to Indian NTP pool: in.pool.ntp.org
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "clock.h"
#include <string.h>

/* Network syscalls (if available) */
extern int sys_socket(int domain, int type, int protocol);
extern int sys_connect(int sock, void *addr, int addrlen);
extern int sys_send(int sock, void *buf, int len, int flags);
extern int sys_recv(int sock, void *buf, int len, int flags);
extern int sys_closesock(int sock);

/* DNS resolution placeholder */
static int dns_resolve(const char *hostname, uint32_t *ip_out) {
    (void)hostname;
    /* 
     * Production implementation would use DNS syscall.
     * For now, use hardcoded IP for in.pool.ntp.org
     * 162.159.200.1 (Cloudflare NTP)
     */
    *ip_out = 0xA29FC801;  /* 162.159.200.1 in network order */
    return 0;
}

/* NTP packet structure */
typedef struct __attribute__((packed)) {
    uint8_t  li_vn_mode;     /* Leap indicator, version, mode */
    uint8_t  stratum;
    uint8_t  poll;
    int8_t   precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint32_t ref_timestamp_sec;
    uint32_t ref_timestamp_frac;
    uint32_t orig_timestamp_sec;
    uint32_t orig_timestamp_frac;
    uint32_t recv_timestamp_sec;
    uint32_t recv_timestamp_frac;
    uint32_t tx_timestamp_sec;
    uint32_t tx_timestamp_frac;
} ntp_packet_t;

/* Byte swap for network order */
static uint32_t ntohl(uint32_t n) {
    return ((n >> 24) & 0xFF) |
           ((n >> 8) & 0xFF00) |
           ((n << 8) & 0xFF0000) |
           ((n << 24) & 0xFF000000);
}

/* Initialize NTP client */
int ntp_init(clock_ctx_t *ctx) {
    if (!ctx) return -1;
    
    ctx->ntp_synced = false;
    ctx->last_sync = 0;
    
    return 0;
}

/* Perform NTP synchronization */
int ntp_sync(clock_ctx_t *ctx) {
    if (!ctx || !ctx->ntp_config.enabled) return -1;
    
    /* Resolve NTP server */
    uint32_t server_ip;
    if (dns_resolve(ctx->ntp_config.server, &server_ip) != 0) {
        return -1;
    }
    
    /* Create UDP socket */
    int sock = sys_socket(2, 2, 17);  /* AF_INET, SOCK_DGRAM, IPPROTO_UDP */
    if (sock < 0) {
        return -1;
    }
    
    /* Prepare NTP request packet */
    ntp_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    
    /* Set NTP version 3, client mode */
    packet.li_vn_mode = 0x1B;  /* LI=0, VN=3, Mode=3 (client) */
    
    /* Send NTP request */
    /* Note: Real implementation needs sockaddr_in structure */
    /* This is simplified for the demo */
    
    /* Receive NTP response */
    ntp_packet_t response;
    int recv_len = sys_recv(sock, &response, sizeof(response), 0);
    
    if (recv_len >= (int)sizeof(response)) {
        /* Extract transmit timestamp */
        uint32_t ntp_secs = ntohl(response.tx_timestamp_sec);
        
        /* NTP epoch is 1900, Unix epoch is 1970 (70 years difference) */
        uint32_t unix_secs = ntp_secs - 2208988800U;
        
        /* Add IST offset (5:30 = 330 minutes = 19800 seconds) */
        unix_secs += 19800;
        
        /* Convert to time structure */
        uint32_t days = unix_secs / 86400;
        uint32_t day_secs = unix_secs % 86400;
        
        ctx->current_time.hour = day_secs / 3600;
        ctx->current_time.minute = (day_secs % 3600) / 60;
        ctx->current_time.second = day_secs % 60;
        
        /* Simple date calculation (approximate) */
        ctx->current_time.year = 1970 + (days / 365);
        
        ctx->ntp_synced = true;
        
        extern uint64_t pit_get_ticks(void);
        ctx->last_sync = pit_get_ticks();
    }
    
    sys_closesock(sock);
    
    return ctx->ntp_synced ? 0 : -1;
}

/* Shutdown NTP client */
void ntp_shutdown(clock_ctx_t *ctx) {
    if (ctx) {
        ctx->ntp_config.enabled = false;
    }
}
