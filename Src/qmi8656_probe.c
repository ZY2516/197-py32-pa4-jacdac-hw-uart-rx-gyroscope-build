#include "qmi8656_probe.h"

#include <string.h>

enum
{
  APP_QMI865X_COMPAT_REG_PART_ID = 0x00u,
  APP_QMI865X_COMPAT_PART_ID = 0x05u,
  APP_QMI865X_COMPAT_REG_CTRL1 = 0x02u,
  APP_QMI865X_COMPAT_REG_CTRL2 = 0x03u,
  APP_QMI865X_COMPAT_REG_CTRL3 = 0x04u,
  APP_QMI865X_COMPAT_REG_CTRL5 = 0x06u,
  APP_QMI865X_COMPAT_REG_CTRL7 = 0x08u,
  APP_QMI865X_COMPAT_REG_TEMP_L = 0x33u,
  APP_QMI865X_COMPAT_REG_ACCEL_XL = 0x35u,
  APP_QMI865X_COMPAT_REG_GYRO_XL = 0x3Bu,
  APP_QMI865X_COMPAT_CTRL1_ADDR_AI = 0x40u,
  APP_QMI865X_COMPAT_ODR_1000HZ = 0x03u,
  APP_QMI865X_COMPAT_ACCEL_RANGE = APP_QMI_ACCEL_RANGE_8G,
  APP_QMI865X_COMPAT_GYRO_RANGE = APP_QMI_GYRO_RANGE_512DPS,
  APP_QMI865X_COMPAT_CTRL2_ACCEL_8G_1000HZ =
      (APP_QMI865X_COMPAT_ACCEL_RANGE << 4) | APP_QMI865X_COMPAT_ODR_1000HZ,
  APP_QMI865X_COMPAT_CTRL3_GYRO_512DPS_1000HZ =
      (APP_QMI865X_COMPAT_GYRO_RANGE << 4) | APP_QMI865X_COMPAT_ODR_1000HZ,
  APP_QMI865X_COMPAT_CTRL5_LOWPASS = 0x70u,
  APP_QMI865X_COMPAT_CTRL7_ENABLE_ACCEL_GYRO = 0x03u,
  APP_QMI865X_COMPAT_POST_CONFIG_DELAY_MS = 100u,
  APP_QMI865X_COMPAT_MOTION_READ_RETRIES = 3u,
  APP_QMI865X_COMPAT_MOTION_RETRY_DELAY_MS = 20u
};

static APP_QmiProbeResult g_last_probe_result;
static I2C_HandleTypeDef *g_bound_i2c;

static uint8_t APP_QmiSelectPreferredAddress(uint8_t mask)
{
  if ((mask & APP_QMI_PROBE_ADDR_6B_MASK) != 0u)
  {
    return 0x6Bu;
  }

  if ((mask & APP_QMI_PROBE_ADDR_6A_MASK) != 0u)
  {
    return 0x6Au;
  }

  return 0u;
}

static int16_t APP_QmiDecodeLeS16(const uint8_t *bytes)
{
  return (int16_t)(((uint16_t)bytes[1] << 8) | bytes[0]);
}

static uint32_t APP_QmiAccelCountsPerG(uint8_t accel_range)
{
  switch (accel_range)
  {
  case APP_QMI_ACCEL_RANGE_2G:
    return 16384u;
  case APP_QMI_ACCEL_RANGE_4G:
    return 8192u;
  case APP_QMI_ACCEL_RANGE_8G:
    return 4096u;
  case APP_QMI_ACCEL_RANGE_16G:
    return 2048u;
  default:
    return 0u;
  }
}

static uint32_t APP_QmiGyroCountsPerDps(uint8_t gyro_range)
{
  switch (gyro_range)
  {
  case APP_QMI_GYRO_RANGE_16DPS:
    return 2048u;
  case APP_QMI_GYRO_RANGE_32DPS:
    return 1024u;
  case APP_QMI_GYRO_RANGE_64DPS:
    return 512u;
  case APP_QMI_GYRO_RANGE_128DPS:
    return 256u;
  case APP_QMI_GYRO_RANGE_256DPS:
    return 128u;
  case APP_QMI_GYRO_RANGE_512DPS:
    return 64u;
  case APP_QMI_GYRO_RANGE_1024DPS:
    return 32u;
  case APP_QMI_GYRO_RANGE_2048DPS:
    return 16u;
  default:
    return 0u;
  }
}

static int32_t APP_QmiRawToFixed20(int16_t raw, uint32_t counts_per_unit)
{
  if (counts_per_unit == 0u)
  {
    return 0;
  }

  return (int32_t)(((int64_t)raw << 20) / (int64_t)counts_per_unit);
}

static void APP_QmiProbeDelayMs(uint32_t delay_ms)
{
  if (delay_ms != 0u)
  {
    HAL_Delay(delay_ms);
  }
}

static HAL_StatusTypeDef APP_QmiProbeReadBlock(I2C_HandleTypeDef *hi2c,
                                               uint8_t addr_7bit,
                                               uint8_t start_reg,
                                               uint8_t *buffer,
                                               uint16_t len)
{
  HAL_StatusTypeDef status;

  if ((hi2c == NULL) || (buffer == NULL) || (len == 0u))
  {
    return HAL_ERROR;
  }

  status = HAL_I2C_Mem_Read(hi2c,
                            (uint16_t)(addr_7bit << 1),
                            start_reg,
                            I2C_MEMADD_SIZE_8BIT,
                            buffer,
                            len,
                            20u);
  if (status == HAL_OK)
  {
    return HAL_OK;
  }

  status = HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr_7bit << 1), &start_reg, 1u, 20u);
  if (status != HAL_OK)
  {
    return status;
  }

  return HAL_I2C_Master_Receive(hi2c, (uint16_t)(addr_7bit << 1), buffer, len, 20u);
}

static HAL_StatusTypeDef APP_QmiProbeWriteCompatConfig(I2C_HandleTypeDef *hi2c, uint8_t addr_7bit)
{
  HAL_StatusTypeDef status;

  status = APP_QmiProbeWriteReg(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_CTRL1, APP_QMI865X_COMPAT_CTRL1_ADDR_AI);
  if (status != HAL_OK)
  {
    return status;
  }

  status = APP_QmiProbeWriteReg(hi2c,
                                addr_7bit,
                                APP_QMI865X_COMPAT_REG_CTRL2,
                                APP_QMI865X_COMPAT_CTRL2_ACCEL_8G_1000HZ);
  if (status != HAL_OK)
  {
    return status;
  }

  status = APP_QmiProbeWriteReg(hi2c,
                                addr_7bit,
                                APP_QMI865X_COMPAT_REG_CTRL3,
                                APP_QMI865X_COMPAT_CTRL3_GYRO_512DPS_1000HZ);
  if (status != HAL_OK)
  {
    return status;
  }

  status = APP_QmiProbeWriteReg(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_CTRL5, APP_QMI865X_COMPAT_CTRL5_LOWPASS);
  if (status != HAL_OK)
  {
    return status;
  }

  return APP_QmiProbeWriteReg(hi2c,
                              addr_7bit,
                              APP_QMI865X_COMPAT_REG_CTRL7,
                              APP_QMI865X_COMPAT_CTRL7_ENABLE_ACCEL_GYRO);
}

static HAL_StatusTypeDef APP_QmiProbeVerifyCompatConfig(I2C_HandleTypeDef *hi2c, uint8_t addr_7bit)
{
  uint8_t value = 0u;
  HAL_StatusTypeDef status;

  status = APP_QmiProbeReadReg(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_CTRL1, &value);
  if ((status != HAL_OK) || (value != APP_QMI865X_COMPAT_CTRL1_ADDR_AI))
  {
    return HAL_ERROR;
  }

  status = APP_QmiProbeReadReg(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_CTRL2, &value);
  if ((status != HAL_OK) || (value != APP_QMI865X_COMPAT_CTRL2_ACCEL_8G_1000HZ))
  {
    return HAL_ERROR;
  }

  status = APP_QmiProbeReadReg(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_CTRL3, &value);
  if ((status != HAL_OK) || (value != APP_QMI865X_COMPAT_CTRL3_GYRO_512DPS_1000HZ))
  {
    return HAL_ERROR;
  }

  status = APP_QmiProbeReadReg(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_CTRL5, &value);
  if ((status != HAL_OK) || (value != APP_QMI865X_COMPAT_CTRL5_LOWPASS))
  {
    return HAL_ERROR;
  }

  status = APP_QmiProbeReadReg(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_CTRL7, &value);
  if ((status != HAL_OK) || (value != APP_QMI865X_COMPAT_CTRL7_ENABLE_ACCEL_GYRO))
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

static HAL_StatusTypeDef APP_QmiProbeReadMotionRawInternal(I2C_HandleTypeDef *hi2c,
                                                           uint8_t addr_7bit,
                                                           int16_t accel_raw[3],
                                                           int16_t gyro_raw[3])
{
  uint8_t accel_buffer[6];
  uint8_t gyro_buffer[6];
  HAL_StatusTypeDef status;

  // Keep the motion read path aligned with the validated ESP32 QMI8658 reference:
  // read accel and gyro as two separate 6-byte windows instead of one 12-byte sweep.
  status = APP_QmiProbeReadBlock(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_ACCEL_XL, accel_buffer, sizeof(accel_buffer));
  if (status != HAL_OK)
  {
    return status;
  }

  accel_raw[0] = APP_QmiDecodeLeS16(&accel_buffer[0]);
  accel_raw[1] = APP_QmiDecodeLeS16(&accel_buffer[2]);
  accel_raw[2] = APP_QmiDecodeLeS16(&accel_buffer[4]);

  status = APP_QmiProbeReadBlock(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_GYRO_XL, gyro_buffer, sizeof(gyro_buffer));
  if (status != HAL_OK)
  {
    return status;
  }

  gyro_raw[0] = APP_QmiDecodeLeS16(&gyro_buffer[0]);
  gyro_raw[1] = APP_QmiDecodeLeS16(&gyro_buffer[2]);
  gyro_raw[2] = APP_QmiDecodeLeS16(&gyro_buffer[4]);
  return HAL_OK;
}

static HAL_StatusTypeDef APP_QmiProbeReadTemperatureRawInternal(I2C_HandleTypeDef *hi2c,
                                                                uint8_t addr_7bit,
                                                                int16_t *temperature_raw)
{
  uint8_t buffer[2];
  HAL_StatusTypeDef status;

  if (temperature_raw == NULL)
  {
    return HAL_ERROR;
  }

  status = APP_QmiProbeReadBlock(hi2c, addr_7bit, APP_QMI865X_COMPAT_REG_TEMP_L, buffer, sizeof(buffer));
  if (status != HAL_OK)
  {
    return status;
  }

  *temperature_raw = APP_QmiDecodeLeS16(buffer);
  return HAL_OK;
}

static HAL_StatusTypeDef APP_QmiProbeReadMotionRawWithRetry(I2C_HandleTypeDef *hi2c,
                                                            uint8_t addr_7bit,
                                                            int16_t accel_raw[3],
                                                            int16_t gyro_raw[3])
{
  HAL_StatusTypeDef status = HAL_ERROR;
  uint32_t attempt = 0u;

  for (attempt = 0u; attempt < APP_QMI865X_COMPAT_MOTION_READ_RETRIES; ++attempt)
  {
    status = APP_QmiProbeReadMotionRawInternal(hi2c, addr_7bit, accel_raw, gyro_raw);
    if (status == HAL_OK)
    {
      return HAL_OK;
    }

    APP_QmiProbeDelayMs(APP_QMI865X_COMPAT_MOTION_RETRY_DELAY_MS);
  }

  return status;
}

static void APP_QmiProbeApplyCandidateResult(APP_QmiProbeResult *result,
                                             uint8_t addr_7bit,
                                             HAL_StatusTypeDef status,
                                             uint8_t config_write_ok,
                                             uint8_t config_readback_ok,
                                             uint8_t motion_read_ok,
                                             const int16_t accel_raw[3],
                                             const int16_t gyro_raw[3],
                                             uint8_t temperature_valid,
                                             int16_t temperature_raw)
{
  if (result == NULL)
  {
    return;
  }

  result->selected_addr_7bit = addr_7bit;
  result->config_attempted = 1u;
  result->config_write_ok = config_write_ok;
  result->config_readback_ok = config_readback_ok;
  result->motion_read_ok = motion_read_ok;
  result->temperature_valid = temperature_valid;
  result->temperature_raw = temperature_raw;
  result->last_status = status;

  if ((motion_read_ok != 0u) && (accel_raw != NULL) && (gyro_raw != NULL))
  {
    memcpy(result->accel_raw, accel_raw, sizeof(result->accel_raw));
    memcpy(result->gyro_raw, gyro_raw, sizeof(result->gyro_raw));
  }
}

static HAL_StatusTypeDef APP_QmiProbeTryCandidate(I2C_HandleTypeDef *hi2c,
                                                  APP_QmiProbeResult *result,
                                                  uint8_t addr_7bit)
{
  HAL_StatusTypeDef status;
  int16_t accel_raw[3] = {0};
  int16_t gyro_raw[3] = {0};
  int16_t temperature_raw = 0;
  uint8_t temperature_valid = 0u;

  status = APP_QmiProbeWriteCompatConfig(hi2c, addr_7bit);
  if (status != HAL_OK)
  {
    APP_QmiProbeApplyCandidateResult(result, addr_7bit, status, 0u, 0u, 0u, accel_raw, gyro_raw, 0u, 0);
    return status;
  }

  APP_QmiProbeDelayMs(APP_QMI865X_COMPAT_POST_CONFIG_DELAY_MS);
  status = APP_QmiProbeVerifyCompatConfig(hi2c, addr_7bit);
  if (status != HAL_OK)
  {
    APP_QmiProbeApplyCandidateResult(result, addr_7bit, status, 1u, 0u, 0u, accel_raw, gyro_raw, 0u, 0);
    return status;
  }

  APP_QmiProbeDelayMs(APP_QMI865X_COMPAT_POST_CONFIG_DELAY_MS);
  status = APP_QmiProbeReadMotionRawWithRetry(hi2c, addr_7bit, accel_raw, gyro_raw);
  if (status == HAL_OK)
  {
    if (APP_QmiProbeReadTemperatureRawInternal(hi2c, addr_7bit, &temperature_raw) == HAL_OK)
    {
      temperature_valid = 1u;
    }

    APP_QmiProbeApplyCandidateResult(
        result, addr_7bit, HAL_OK, 1u, 1u, 1u, accel_raw, gyro_raw, temperature_valid, temperature_raw);
    return HAL_OK;
  }

  APP_QmiProbeApplyCandidateResult(result, addr_7bit, status, 1u, 1u, 0u, accel_raw, gyro_raw, 0u, 0);
  return status;
}

HAL_StatusTypeDef APP_QmiProbeReadReg(I2C_HandleTypeDef *hi2c,
                                      uint8_t addr_7bit,
                                      uint8_t reg,
                                      uint8_t *value)
{
  if ((hi2c == NULL) || (value == NULL))
  {
    return HAL_ERROR;
  }

  return APP_QmiProbeReadBlock(hi2c, addr_7bit, reg, value, 1u);
}

HAL_StatusTypeDef APP_QmiProbeWriteReg(I2C_HandleTypeDef *hi2c,
                                       uint8_t addr_7bit,
                                       uint8_t reg,
                                       uint8_t value)
{
  uint8_t tx_buffer[2];
  HAL_StatusTypeDef status;

  if (hi2c == NULL)
  {
    return HAL_ERROR;
  }

  status = HAL_I2C_Mem_Write(hi2c,
                             (uint16_t)(addr_7bit << 1),
                             reg,
                             I2C_MEMADD_SIZE_8BIT,
                             &value,
                             1,
                             20u);
  if (status == HAL_OK)
  {
    return HAL_OK;
  }

  tx_buffer[0] = reg;
  tx_buffer[1] = value;
  return HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr_7bit << 1), tx_buffer, sizeof(tx_buffer), 20u);
}

void APP_QmiProbeBindBus(I2C_HandleTypeDef *hi2c)
{
  g_bound_i2c = hi2c;
}

void APP_QmiProbeScan(I2C_HandleTypeDef *hi2c, APP_QmiProbeResult *result)
{
  uint8_t addr = 0u;
  uint8_t part_id = 0u;
  uint8_t candidates[2] = {0u, 0u};
  uint32_t candidate_count = 0u;
  uint32_t candidate_index = 0u;
  HAL_StatusTypeDef status = HAL_ERROR;

  if ((hi2c == NULL) || (result == NULL))
  {
    return;
  }

  memset(result, 0, sizeof(*result));
  result->last_status = HAL_ERROR;
  result->accel_range = APP_QMI865X_COMPAT_ACCEL_RANGE;
  result->gyro_range = APP_QMI865X_COMPAT_GYRO_RANGE;

  addr = 0x6Au;
  status = HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(addr << 1), 2u, 20u);
  if (status == HAL_OK)
  {
    result->ready_mask |= APP_QMI_PROBE_ADDR_6A_MASK;
    status = APP_QmiProbeReadReg(hi2c, addr, APP_QMI865X_COMPAT_REG_PART_ID, &part_id);
    if (status == HAL_OK)
    {
      if ((result->part_id_valid == 0u) || (part_id == APP_QMI865X_COMPAT_PART_ID))
      {
        result->part_id = part_id;
        result->part_id_valid = 1u;
      }

      if (part_id == APP_QMI865X_COMPAT_PART_ID)
      {
        result->compat_match_mask |= APP_QMI_PROBE_ADDR_6A_MASK;
      }
      else
      {
        result->compat_mismatch_mask |= APP_QMI_PROBE_ADDR_6A_MASK;
      }
    }

    result->last_status = status;
  }

  addr = 0x6Bu;
  status = HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(addr << 1), 2u, 20u);
  if (status == HAL_OK)
  {
    result->ready_mask |= APP_QMI_PROBE_ADDR_6B_MASK;
    status = APP_QmiProbeReadReg(hi2c, addr, APP_QMI865X_COMPAT_REG_PART_ID, &part_id);
    if (status == HAL_OK)
    {
      if ((result->part_id_valid == 0u) || (part_id == APP_QMI865X_COMPAT_PART_ID))
      {
        result->part_id = part_id;
        result->part_id_valid = 1u;
      }

      if (part_id == APP_QMI865X_COMPAT_PART_ID)
      {
        result->compat_match_mask |= APP_QMI_PROBE_ADDR_6B_MASK;
      }
      else
      {
        result->compat_mismatch_mask |= APP_QMI_PROBE_ADDR_6B_MASK;
      }
    }

    result->last_status = status;
  }

  if (result->compat_match_mask != 0u)
  {
    result->selected_addr_7bit = APP_QmiSelectPreferredAddress(result->compat_match_mask);
  }
  else
  {
    result->selected_addr_7bit = APP_QmiSelectPreferredAddress(result->ready_mask);
  }

  if ((result->compat_match_mask & APP_QMI_PROBE_ADDR_6B_MASK) != 0u)
  {
    candidates[candidate_count++] = 0x6Bu;
  }
  else if ((result->compat_match_mask & APP_QMI_PROBE_ADDR_6A_MASK) != 0u)
  {
    candidates[candidate_count++] = 0x6Au;
  }
  else if ((result->ready_mask & APP_QMI_PROBE_ADDR_6B_MASK) != 0u)
  {
    candidates[candidate_count++] = 0x6Bu;
  }
  else if ((result->ready_mask & APP_QMI_PROBE_ADDR_6A_MASK) != 0u)
  {
    candidates[candidate_count++] = 0x6Au;
  }

  if (candidate_count != 0u)
  {
    for (candidate_index = 0u; candidate_index < candidate_count; ++candidate_index)
    {
      status = APP_QmiProbeTryCandidate(hi2c, result, candidates[candidate_index]);
      if (status == HAL_OK)
      {
        break;
      }
    }
  }

  g_last_probe_result = *result;
}

const APP_QmiProbeResult *APP_QmiProbeGetLastResult(void)
{
  return &g_last_probe_result;
}

HAL_StatusTypeDef APP_QmiProbeEnsureConfigured(void)
{
  HAL_StatusTypeDef status;

  if ((g_bound_i2c == NULL) || (g_last_probe_result.selected_addr_7bit == 0u))
  {
    return HAL_ERROR;
  }

  if (g_last_probe_result.config_readback_ok != 0u)
  {
    return HAL_OK;
  }

  g_last_probe_result.config_attempted = 1u;
  status = APP_QmiProbeWriteCompatConfig(g_bound_i2c, g_last_probe_result.selected_addr_7bit);
  if (status != HAL_OK)
  {
    g_last_probe_result.last_status = status;
    return status;
  }

  g_last_probe_result.config_write_ok = 1u;
  APP_QmiProbeDelayMs(APP_QMI865X_COMPAT_POST_CONFIG_DELAY_MS);
  status = APP_QmiProbeVerifyCompatConfig(g_bound_i2c, g_last_probe_result.selected_addr_7bit);
  if (status == HAL_OK)
  {
    g_last_probe_result.config_readback_ok = 1u;
  }

  g_last_probe_result.last_status = status;
  return status;
}

HAL_StatusTypeDef APP_QmiProbeReadMotionFixed20(int32_t accel_out[3], int32_t gyro_out[3])
{
  HAL_StatusTypeDef status;
  int16_t accel_raw[3];
  int16_t gyro_raw[3];
  const uint32_t accel_counts_per_g = APP_QmiAccelCountsPerG(g_last_probe_result.accel_range);
  const uint32_t gyro_counts_per_dps = APP_QmiGyroCountsPerDps(g_last_probe_result.gyro_range);

  if ((g_bound_i2c == NULL) || (accel_out == NULL) || (gyro_out == NULL))
  {
    return HAL_ERROR;
  }

  if ((accel_counts_per_g == 0u) || (gyro_counts_per_dps == 0u))
  {
    return HAL_ERROR;
  }

  status = APP_QmiProbeEnsureConfigured();
  if (status != HAL_OK)
  {
    return status;
  }

  status = APP_QmiProbeReadMotionRawInternal(g_bound_i2c, g_last_probe_result.selected_addr_7bit, accel_raw, gyro_raw);
  if (status != HAL_OK)
  {
    status =
        APP_QmiProbeReadMotionRawWithRetry(g_bound_i2c, g_last_probe_result.selected_addr_7bit, accel_raw, gyro_raw);
  }
  if (status != HAL_OK)
  {
    g_last_probe_result.motion_read_ok = 0u;
    g_last_probe_result.last_status = status;
    return status;
  }

  memcpy(g_last_probe_result.accel_raw, accel_raw, sizeof(accel_raw));
  memcpy(g_last_probe_result.gyro_raw, gyro_raw, sizeof(gyro_raw));
  g_last_probe_result.motion_read_ok = 1u;
  g_last_probe_result.last_status = HAL_OK;
  if (APP_QmiProbeReadTemperatureRawInternal(g_bound_i2c,
                                             g_last_probe_result.selected_addr_7bit,
                                             &g_last_probe_result.temperature_raw) == HAL_OK)
  {
    g_last_probe_result.temperature_valid = 1u;
  }
  else
  {
    g_last_probe_result.temperature_valid = 0u;
  }

  accel_out[0] = APP_QmiRawToFixed20(accel_raw[0], accel_counts_per_g);
  accel_out[1] = APP_QmiRawToFixed20(accel_raw[1], accel_counts_per_g);
  accel_out[2] = APP_QmiRawToFixed20(accel_raw[2], accel_counts_per_g);

  gyro_out[0] = APP_QmiRawToFixed20(gyro_raw[0], gyro_counts_per_dps);
  gyro_out[1] = APP_QmiRawToFixed20(gyro_raw[1], gyro_counts_per_dps);
  gyro_out[2] = APP_QmiRawToFixed20(gyro_raw[2], gyro_counts_per_dps);
  return HAL_OK;
}
