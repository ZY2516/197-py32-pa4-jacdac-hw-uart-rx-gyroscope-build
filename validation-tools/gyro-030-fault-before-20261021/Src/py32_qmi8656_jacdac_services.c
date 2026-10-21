#include <stdint.h>

#include "jacdac/dist/c/gyroscope.h"
#include "interfaces/jd_sensor_api.h"
#include "jdprofile.h"
#include "qmi8656_probe.h"
#include "gyro_calibration.h"

enum
{
  APP_JACDAC_PRODUCT_ID = 0x32516D56u,
  APP_GYRO_FRESH_MS = 60u
};

static const char g_app_device_name[] = "PY32 Jacdac Gyroscope Beacon";
static const char g_app_fw_version[] = "0.3.0-py32-gyro-beacon";

static int32_t g_gyro_reading[3] = {0, 0, 0};
static APP_GyroCalibration g_gyro_calibration;
static uint32_t g_last_gyro_attempt_ms;
static uint8_t g_gyro_sample_valid;

static void APP_MotionInit(void)
{
}

void APP_MotionReadingsUpdate(void)
{
  const APP_QmiProbeResult *sample;
  uint32_t now_ms = HAL_GetTick();
  HAL_StatusTypeDef status;
  unsigned i;

  if ((uint32_t)(now_ms - g_last_gyro_attempt_ms) < APP_GYRO_SAMPLE_MS)
  {
    return;
  }

  /* Throttle failed attempts too. No catch-up bursts after a stalled main loop. */
  g_last_gyro_attempt_ms = now_ms;
  g_gyro_sample_valid = 0;
  status = APP_QmiProbeReadMotionRaw();
  g_last_gyro_attempt_ms = HAL_GetTick();
  if (status != HAL_OK ||
      (uint32_t)(g_last_gyro_attempt_ms - now_ms) > APP_GYRO_FRESH_MS) {
    APP_GyroCalibrationBreak(&g_gyro_calibration);
    return;
  }
  sample = APP_QmiProbeGetLastResult();
  APP_GyroCalibrationUpdate(&g_gyro_calibration, g_last_gyro_attempt_ms,
                              sample->accel_raw, sample->gyro_raw);
  for (i = 0; i < 3; ++i) {
    int32_t bias = g_gyro_calibration.count == APP_GYRO_STILL_SAMPLES ?
                     g_gyro_calibration.sum_or_bias[i] : 0;
    g_gyro_reading[i] = (int32_t)sample->gyro_raw[i] * 16384 - bias;
  }
  g_gyro_sample_valid = 1;
}

static void *APP_GyroscopeGetReading(void)
{
  APP_MotionReadingsUpdate();
  if (!g_gyro_sample_valid ||
      (uint32_t)(HAL_GetTick() - g_last_gyro_attempt_ms) > APP_GYRO_FRESH_MS)
    return 0;
  return g_gyro_reading;
}

static const sensor_api_t g_gyroscope_api = {
  APP_MotionInit,
  0,
  0,
  APP_GyroscopeGetReading,
  0,
  0,
  0,
  0,
  0,
  0,
  "brain-recognition-gyroscope",
};

void app_init_services(void)
{
  /* Board RGB status LED (R=PA5 G=PA7 B=PB5, common anode): make the
   * connected heartbeat a full FAST green blink instead of the default
   * FAINT one so the link state is clearly visible. */
  jd_connected_blink = JD_BLINK(FAST, 1, GREEN);

  gyroscope_init(&g_gyroscope_api);
}

const char app_dev_class_name[] = "PY32 Jacdac Gyroscope Beacon";
const char app_fw_version[] = "0.3.0-py32-gyro-beacon";

uint32_t app_get_device_class(void)
{
  return APP_JACDAC_PRODUCT_ID;
}

const char *app_get_fw_version(void)
{
  return g_app_fw_version;
}

const char *app_get_dev_class_name(void)
{
  return g_app_device_name;
}

const char *app_get_instance_name(int service_idx)
{
  switch (service_idx)
  {
  case 1:
    return "gyroscope";
  default:
    return 0;
  }
}

void accelerometer_data_transform(int32_t sample[3])
{
  (void)sample;
}

void gyroscope_data_transform(int32_t sample[3])
{
  int32_t x = sample[0];
  int32_t y = sample[1];
  int32_t z = sample[2];

  sample[0] = -y;
  sample[1] = -x;
  sample[2] = -z;
}

int32_t jd_srvcfg_i32(const char *key, int32_t default_value)
{
  (void)key;
  return default_value;
}
