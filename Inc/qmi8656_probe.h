#ifndef APP_QMI8656_PROBE_H
#define APP_QMI8656_PROBE_H

#include "py32f0xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_QMI_PROBE_ADDR_6A_MASK 0x01u
#define APP_QMI_PROBE_ADDR_6B_MASK 0x02u

#define APP_QMI_ACCEL_RANGE_2G   0u
#define APP_QMI_ACCEL_RANGE_4G   1u
#define APP_QMI_ACCEL_RANGE_8G   2u
#define APP_QMI_ACCEL_RANGE_16G  3u

#define APP_QMI_GYRO_RANGE_16DPS    0u
#define APP_QMI_GYRO_RANGE_32DPS    1u
#define APP_QMI_GYRO_RANGE_64DPS    2u
#define APP_QMI_GYRO_RANGE_128DPS   3u
#define APP_QMI_GYRO_RANGE_256DPS   4u
#define APP_QMI_GYRO_RANGE_512DPS   5u
#define APP_QMI_GYRO_RANGE_1024DPS  6u
#define APP_QMI_GYRO_RANGE_2048DPS  7u

typedef struct
{
  uint8_t ready_mask;
  uint8_t compat_match_mask;
  uint8_t compat_mismatch_mask;
  uint8_t selected_addr_7bit;
  uint8_t part_id;
  uint8_t part_id_valid;
  uint8_t config_attempted;
  uint8_t config_write_ok;
  uint8_t config_readback_ok;
  uint8_t motion_read_ok;
  uint8_t accel_range;
  uint8_t gyro_range;
  uint8_t temperature_valid;
  int16_t temperature_raw;
  int16_t accel_raw[3];
  int16_t gyro_raw[3];
  HAL_StatusTypeDef last_status;
} APP_QmiProbeResult;

void APP_QmiProbeBindBus(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef APP_QmiProbeReadReg(I2C_HandleTypeDef *hi2c,
                                      uint8_t addr_7bit,
                                      uint8_t reg,
                                      uint8_t *value);
HAL_StatusTypeDef APP_QmiProbeWriteReg(I2C_HandleTypeDef *hi2c,
                                       uint8_t addr_7bit,
                                       uint8_t reg,
                                       uint8_t value);
void APP_QmiProbeScan(I2C_HandleTypeDef *hi2c, APP_QmiProbeResult *result);
const APP_QmiProbeResult *APP_QmiProbeGetLastResult(void);
HAL_StatusTypeDef APP_QmiProbeEnsureConfigured(void);
HAL_StatusTypeDef APP_QmiProbeReadMotionFixed20(int32_t accel_out[3], int32_t gyro_out[3]);

#ifdef __cplusplus
}
#endif

#endif /* APP_QMI8656_PROBE_H */
