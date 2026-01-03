/*
 * NXNFC Kernel Driver (nxnfc_kdrv)
 * 
 * NFC driver for NeolyxOS
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXNFC_KDRV_H
#define NXNFC_KDRV_H

#include <stdint.h>

#define NXNFC_KDRV_VERSION "1.0.0"
#define NXNFC_MAX_DATA     256

typedef enum {
    NXNFC_TAG_NONE = 0,
    NXNFC_TAG_MIFARE,
    NXNFC_TAG_ISO14443A,
    NXNFC_TAG_ISO14443B,
    NXNFC_TAG_FELICA,
    NXNFC_TAG_NDEF
} nxnfc_tag_type_t;

typedef struct {
    nxnfc_tag_type_t type;
    uint8_t  uid[10];
    uint8_t  uid_len;
    uint8_t  data[NXNFC_MAX_DATA];
    uint16_t data_len;
} nxnfc_tag_t;

int  nxnfc_kdrv_init(void);
void nxnfc_kdrv_shutdown(void);
int  nxnfc_kdrv_poll(nxnfc_tag_t *tag);
int  nxnfc_kdrv_write(const uint8_t *data, uint16_t len);
void nxnfc_kdrv_debug(void);

#endif /* NXNFC_KDRV_H */
