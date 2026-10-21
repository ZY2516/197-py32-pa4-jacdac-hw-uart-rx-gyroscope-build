#include <stdint.h>

#include "jacdac/dist/c/gyroscope.h"
#include "interfaces/jd_sensor_api.h"
#include "jdprofile.h"
#include "qmi8656_probe.h"

enum
{
  APP_JACDAC_PRODUCT_ID = 0x32516D56u,
  APP_GYRO_UPDATE_INTERVAL_MS = 20u,
  APP_GYRO_BIAS_SAMPLE_COUNT = 32u
};

static const char g_app_device_name[] = "PY32 Jacdac Gyroscope Beacon";
static const char g_app_fw_version[] = "0.2.1-py32-gyro-beacon";

static int32_t g_gyro_reading[3] = {0, 0, 0};
static int64_t g_gyro_bias_sum[3];
static int32_t g_gyro_bias[3];
static uint32_t g_last_gyro_update_ms;
static uint8_t g_gyro_has_sample;
static uint8_t g_gyro_bias_sample_count;
static uint8_t g_gyro_bias_ready;

static void APP_MotionInit(void)
{
}

void APP_MotionReadingsUpdate(void)
{
  int32_t accel_unused[3];
  int32_t gyro_sample[3];
  uint32_t now_ms = HAL_GetTick();
  HAL_StatusTypeDef status;

  if ((g_gyro_has_sample != 0u) &&
      ((uint32_t)(now_ms - g_last_gyro_update_ms) < APP_GYRO_UPDATE_INTERVAL_MS))
  {
    return;
  }

  status = APP_QmiProbeReadMotionFixed20(accel_unused, gyro_sample);
  if (status == HAL_OK)
  {
    g_last_gyro_update_ms = now_ms;
    g_gyro_has_sample = 1u;

    if (g_gyro_bias_ready == 0u)
    {
      g_gyro_bias_sum[0] += gyro_sample[0];
      g_gyro_bias_sum[1] += gyro_sample[1];
      g_gyro_bias_sum[2] += gyro_sample[2];
      g_gyro_bias_sample_count++;

      if (g_gyro_bias_sample_count >= APP_GYRO_BIAS_SAMPLE_COUNT)
      {
        g_gyro_bias[0] = (int32_t)(g_gyro_bias_sum[0] / (int64_t)APP_GYRO_BIAS_SAMPLE_COUNT);
        g_gyro_bias[1] = (int32_t)(g_gyro_bias_sum[1] / (int64_t)APP_GYRO_BIAS_SAMPLE_COUNT);
        g_gyro_bias[2] = (int32_t)(g_gyro_bias_sum[2] / (int64_t)APP_GYRO_BIAS_SAMPLE_COUNT);
        g_gyro_bias_ready = 1u;
      }

      g_gyro_reading[0] = 0;
      g_gyro_reading[1] = 0;
      g_gyro_reading[2] = 0;
    }
    else
    {
      g_gyro_reading[0] = gyro_sample[0] - g_gyro_bias[0];
      g_gyro_reading[1] = gyro_sample[1] - g_gyro_bias[1];
      g_gyro_reading[2] = gyro_sample[2] - g_gyro_bias[2];
    }
  }
}

static void *APP_GyroscopeGetReading(void)
{
  APP_MotionReadingsUpdate();
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
const char app_fw_version[] = "0.2.1-py32-gyro-beacon";

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
