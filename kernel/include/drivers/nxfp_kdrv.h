/*
 * NXFingerprint Kernel Driver (nxfp_kdrv)
 * 
 * Fingerprint sensor driver for NeolyxOS
 * Supports: Enrollment, authentication, template storage
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXFP_KDRV_H
#define NXFP_KDRV_H

#include <stdint.h>

#define NXFP_KDRV_VERSION  "1.0.0"
#define NXFP_MAX_TEMPLATES 10
#define NXFP_TEMPLATE_SIZE 512

/* Fingerprint sensor types */
typedef enum {
    NXFP_TYPE_NONE = 0,
    NXFP_TYPE_OPTICAL,
    NXFP_TYPE_CAPACITIVE,
    NXFP_TYPE_ULTRASONIC,
    NXFP_TYPE_THERMAL
} nxfp_type_t;

/* Authentication result */
typedef enum {
    NXFP_RESULT_NO_FINGER = 0,
    NXFP_RESULT_MATCH,
    NXFP_RESULT_NO_MATCH,
    NXFP_RESULT_ERROR,
    NXFP_RESULT_PARTIAL
} nxfp_result_t;

/* Fingerprint template */
typedef struct {
    uint32_t id;
    uint32_t user_id;
    uint8_t  data[NXFP_TEMPLATE_SIZE];
    uint32_t quality;
    uint64_t created_at;
} nxfp_template_t;

/* Device info */
typedef struct {
    char         name[64];
    nxfp_type_t  type;
    uint16_t     sensor_width;
    uint16_t     sensor_height;
    uint8_t      template_count;
    uint8_t      connected;
} nxfp_info_t;

/* API */
int  nxfp_kdrv_init(void);
void nxfp_kdrv_shutdown(void);
int  nxfp_kdrv_get_info(nxfp_info_t *info);

/* Enrollment */
int  nxfp_kdrv_start_enroll(uint32_t user_id);
int  nxfp_kdrv_capture_enroll(void);
int  nxfp_kdrv_finish_enroll(uint32_t *template_id);
void nxfp_kdrv_cancel_enroll(void);

/* Authentication */
nxfp_result_t nxfp_kdrv_authenticate(uint32_t *matched_user);
int nxfp_kdrv_wait_finger(uint32_t timeout_ms);

/* Template management */
int  nxfp_kdrv_delete_template(uint32_t template_id);
int  nxfp_kdrv_delete_user(uint32_t user_id);
void nxfp_kdrv_delete_all(void);

void nxfp_kdrv_debug(void);

#endif /* NXFP_KDRV_H */
