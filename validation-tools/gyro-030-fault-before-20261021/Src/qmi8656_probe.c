#include "qmi8656_probe.h"

#include <string.h>

/* Preserve the validated +/-8g, +/-512dps, 1000Hz configuration and the
 * separate six-byte accel/gyro register windows. No runtime sleep/retry loop. */
enum {
  APP_QMI_IO_TIMEOUT_MS = 3u,
  APP_QMI_RECOVERY_MS = 500u,
  APP_QMI_SETTLE_MS = 100u,
  APP_QMI_STAGE_PROBE = 0u,
  APP_QMI_STAGE_WRITE = 1u,
  APP_QMI_STAGE_VERIFY = 6u
};
static const uint8_t g_config[][2] = {
  {0x02u, 0x40u}, {0x03u, 0x23u}, {0x04u, 0x53u},
  {0x06u, 0x70u}, {0x08u, 0x03u}
};
static APP_QmiProbeResult g_last_probe_result;
static I2C_HandleTypeDef *g_bound_i2c;
static uint32_t g_recovery_ms;
static uint8_t g_config_stage;

static HAL_StatusTypeDef APP_QmiProbeReadBlock(I2C_HandleTypeDef *hi2c,
                                               uint8_t addr, uint8_t reg,
                                               uint8_t *buffer, uint16_t len)
{
  if (!hi2c || !buffer || !len)
    return HAL_ERROR;
  /* Avoid the HAL's separate 25ms BUSY wait when the bus is already stuck. */
  if (__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_BUSY) != RESET)
    return HAL_BUSY;
  return HAL_I2C_Mem_Read(hi2c, (uint16_t)(addr << 1), reg,
                          I2C_MEMADD_SIZE_8BIT, buffer, len, APP_QMI_IO_TIMEOUT_MS);
}

HAL_StatusTypeDef APP_QmiProbeReadReg(I2C_HandleTypeDef *hi2c, uint8_t addr,
                                      uint8_t reg, uint8_t *value)
{
  return APP_QmiProbeReadBlock(hi2c, addr, reg, value, 1u);
}

HAL_StatusTypeDef APP_QmiProbeWriteReg(I2C_HandleTypeDef *hi2c, uint8_t addr,
                                       uint8_t reg, uint8_t value)
{
  if (!hi2c)
    return HAL_ERROR;
  if (__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_BUSY) != RESET)
    return HAL_BUSY;
  return HAL_I2C_Mem_Write(hi2c, (uint16_t)(addr << 1), reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1u, APP_QMI_IO_TIMEOUT_MS);
}

void APP_QmiProbeBindBus(I2C_HandleTypeDef *hi2c)
{
  g_bound_i2c = hi2c;
}

/* Record identity only after an actual register read. Never configure a device
 * with a mismatched part ID merely because it acknowledges the I2C address. */
static HAL_StatusTypeDef APP_QmiProbeIdentity(I2C_HandleTypeDef *hi2c,
                                              APP_QmiProbeResult *result, uint8_t addr)
{
  uint8_t id = 0;
  uint8_t mask = addr == 0x6bu ? APP_QMI_PROBE_ADDR_6B_MASK : APP_QMI_PROBE_ADDR_6A_MASK;
  HAL_StatusTypeDef status = APP_QmiProbeReadReg(hi2c, addr, 0x00u, &id);
  result->last_status = status;
  if (status != HAL_OK) {
    result->ready_mask &= (uint8_t)~mask;
    result->compat_match_mask &= (uint8_t)~mask;
    return status;
  }
  result->ready_mask |= mask;
  result->part_id = id;
  result->part_id_valid = 1;
  if (id != 0x05u) {
    result->compat_mismatch_mask |= mask;
    result->compat_match_mask &= (uint8_t)~mask;
    result->last_status = HAL_ERROR;
    return HAL_ERROR;
  }
  result->compat_match_mask |= mask;
  result->compat_mismatch_mask &= (uint8_t)~mask;
  return HAL_OK;
}

void APP_QmiProbeScan(I2C_HandleTypeDef *hi2c, APP_QmiProbeResult *result)
{
  uint8_t addr;
  if (!hi2c || !result)
    return;
  memset(result, 0, sizeof(*result));
  result->last_status = HAL_ERROR;
  result->accel_range = APP_QMI_ACCEL_RANGE_8G;
  result->gyro_range = APP_QMI_GYRO_RANGE_512DPS;
  /* Probe 6A then 6B so 6B wins when both compatible parts exist. */
  for (addr = 0x6au; addr <= 0x6bu; ++addr) {
    if (APP_QmiProbeIdentity(hi2c, result, addr) == HAL_OK)
      result->selected_addr_7bit = addr;
  }
  if (result->selected_addr_7bit) {
    /* Identity fields describe the selected compatible device, even when the
     * other address returned a mismatched ID or did not acknowledge. */
    result->part_id = 0x05u;
    result->part_id_valid = 1;
    result->last_status = HAL_OK;
  }
  /* Configuration is advanced by the main loop after communication starts. */
  g_config_stage = result->selected_addr_7bit ? APP_QMI_STAGE_WRITE : APP_QMI_STAGE_PROBE;
  g_recovery_ms = HAL_GetTick();
  g_last_probe_result = *result;
}

const APP_QmiProbeResult *APP_QmiProbeGetLastResult(void)
{
  return &g_last_probe_result;
}

static HAL_StatusTypeDef APP_QmiProbeFault(HAL_StatusTypeDef status)
{
  g_last_probe_result.motion_read_ok = 0;
  g_last_probe_result.config_write_ok = 0;
  g_last_probe_result.config_readback_ok = 0;
  g_last_probe_result.temperature_valid = 0;
  g_last_probe_result.last_status = status;
  g_config_stage = APP_QMI_STAGE_PROBE;
  g_recovery_ms = HAL_GetTick();
  return status;
}

HAL_StatusTypeDef APP_QmiProbeEnsureConfigured(void)
{
  HAL_StatusTypeDef status;
  uint8_t value = 0;
  uint8_t index;
  uint32_t now_ms = HAL_GetTick();
  if (!g_bound_i2c)
    return HAL_ERROR;
  if (g_last_probe_result.config_readback_ok) {
    /* Detect a sensor brownout/reset even if it continues to acknowledge reads.
     * Check one register per second, without adding state or pausing sampling. */
    if ((uint32_t)(now_ms - g_recovery_ms) >= 1000u) {
      index = g_config_stage - (APP_QMI_STAGE_VERIFY + 5u);
      status = APP_QmiProbeReadReg(g_bound_i2c, g_last_probe_result.selected_addr_7bit,
                                    g_config[index][0], &value);
      if (status != HAL_OK || value != g_config[index][1])
        return APP_QmiProbeFault(status == HAL_OK ? HAL_ERROR : status);
      g_recovery_ms = HAL_GetTick();
      if (++g_config_stage == APP_QMI_STAGE_VERIFY + 10u)
        g_config_stage = APP_QMI_STAGE_VERIFY + 5u;
    }
    return HAL_OK;
  }

  if (g_config_stage == APP_QMI_STAGE_PROBE) {
    if ((uint32_t)(now_ms - g_recovery_ms) < APP_QMI_RECOVERY_MS)
      return HAL_BUSY;
    g_recovery_ms = now_ms;
    /* Alternate addresses on successive recovery attempts, including absent at boot. */
    g_last_probe_result.selected_addr_7bit =
      g_last_probe_result.selected_addr_7bit == 0x6bu ? 0x6au : 0x6bu;
    g_last_probe_result.part_id_valid = 0;
    status = APP_QmiProbeIdentity(g_bound_i2c, &g_last_probe_result,
                                   g_last_probe_result.selected_addr_7bit);
    if (status == HAL_OK)
      g_config_stage = APP_QMI_STAGE_WRITE;
    return HAL_BUSY;
  }

  /* Each invocation performs at most one config transaction. The service calls
   * this at 20ms intervals; the 100ms settling period uses elapsed time. */
  g_last_probe_result.config_attempted = 1;
  if (g_config_stage < APP_QMI_STAGE_VERIFY) {
    index = g_config_stage - APP_QMI_STAGE_WRITE;
    status = APP_QmiProbeWriteReg(g_bound_i2c, g_last_probe_result.selected_addr_7bit,
                                   g_config[index][0], g_config[index][1]);
    if (status != HAL_OK)
      return APP_QmiProbeFault(status);
    ++g_config_stage;
    g_recovery_ms = HAL_GetTick();
    if (g_config_stage == APP_QMI_STAGE_VERIFY)
      g_last_probe_result.config_write_ok = 1;
  } else {
    if (g_config_stage == APP_QMI_STAGE_VERIFY &&
        (uint32_t)(now_ms - g_recovery_ms) < APP_QMI_SETTLE_MS)
      return HAL_BUSY;
    index = g_config_stage - APP_QMI_STAGE_VERIFY;
    status = APP_QmiProbeReadReg(g_bound_i2c, g_last_probe_result.selected_addr_7bit,
                                  g_config[index][0], &value);
    if (status != HAL_OK || value != g_config[index][1])
      return APP_QmiProbeFault(status == HAL_OK ? HAL_ERROR : status);
    if (++g_config_stage == APP_QMI_STAGE_VERIFY + 5u) {
      g_last_probe_result.config_readback_ok = 1;
      g_last_probe_result.last_status = HAL_OK;
    }
  }
  return HAL_BUSY;
}

HAL_StatusTypeDef APP_QmiProbeReadMotionRaw(void)
{
  uint8_t bytes[12];
  unsigned i;
  HAL_StatusTypeDef status;
  g_last_probe_result.motion_read_ok = 0;
  status = APP_QmiProbeEnsureConfigured();
  if (status != HAL_OK)
    return status;
  status = APP_QmiProbeReadBlock(g_bound_i2c, g_last_probe_result.selected_addr_7bit,
                                  0x35u, bytes, 6u);
  if (status == HAL_OK)
    status = APP_QmiProbeReadBlock(g_bound_i2c, g_last_probe_result.selected_addr_7bit,
                                    0x3bu, bytes + 6, 6u);
  if (status != HAL_OK)
    return APP_QmiProbeFault(status);
  /* Commit both vectors only after both transactions succeed. Temperature is
   * unused by the published profile; it is no longer read on every sample. */
  for (i = 0; i < 3; ++i) {
    g_last_probe_result.accel_raw[i] = (int16_t)((uint16_t)bytes[2*i] | (uint16_t)bytes[2*i+1] << 8);
    g_last_probe_result.gyro_raw[i] = (int16_t)((uint16_t)bytes[6+2*i] | (uint16_t)bytes[7+2*i] << 8);
  }
  g_last_probe_result.motion_read_ok = 1;
  g_last_probe_result.last_status = HAL_OK;
  return HAL_OK;
}

HAL_StatusTypeDef APP_QmiProbeReadMotionFixed20(int32_t accel_out[3], int32_t gyro_out[3])
{
  unsigned i;
  HAL_StatusTypeDef status;
  if (!accel_out || !gyro_out)
    return HAL_ERROR;
  status = APP_QmiProbeReadMotionRaw();
  if (status != HAL_OK)
    return status;
  for (i = 0; i < 3; ++i) {
    accel_out[i] = (int32_t)g_last_probe_result.accel_raw[i] * 256;
    gyro_out[i] = (int32_t)g_last_probe_result.gyro_raw[i] * 16384;
  }
  return HAL_OK;
}
