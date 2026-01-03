/*
 * NXSensors Kernel Driver (nxsensor_kdrv)
 * 
 * Motion sensors: Accelerometer, Gyroscope, Magnetometer
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXSENSOR_KDRV_H
#define NXSENSOR_KDRV_H

#include <stdint.h>

#define NXSENSOR_KDRV_VERSION "1.0.0"

typedef struct {
    int16_t x, y, z;
} nxsensor_vec3_t;

typedef struct {
    nxsensor_vec3_t accel;     /* mg (milli-g) */
    nxsensor_vec3_t gyro;      /* mdps (milli-deg/sec) */
    nxsensor_vec3_t mag;       /* uT (micro-Tesla) */
    int16_t         temp;      /* 0.1 C */
    uint32_t        timestamp;
} nxsensor_data_t;

typedef struct {
    uint8_t has_accel;
    uint8_t has_gyro;
    uint8_t has_mag;
    uint8_t has_temp;
    uint16_t accel_range;
    uint16_t gyro_range;
} nxsensor_caps_t;

int  nxsensor_kdrv_init(void);
void nxsensor_kdrv_shutdown(void);
int  nxsensor_kdrv_get_caps(nxsensor_caps_t *caps);
int  nxsensor_kdrv_read(nxsensor_data_t *data);
void nxsensor_kdrv_debug(void);

#endif /* NXSENSOR_KDRV_H */
