#ifndef APP_GYRO_CALIBRATION_H
#define APP_GYRO_CALIBRATION_H

#include <stdint.h>

/* Fixed hardware configuration: +/-8 g (4096 LSB/g), +/-512 dps (64 LSB/dps). */
enum {
  APP_GYRO_SAMPLE_MS = 20,
  APP_GYRO_MAX_GAP_MS = 60,
  APP_GYRO_STILL_DWELL_MS = 600, /* qualify stillness before collecting bias */
  APP_GYRO_STILL_SAMPLES = 81, /* calibration samples, after the 600ms dwell */
  /* Production sample: ~7.5 dps initial X bias, 0.12-0.16 dps RMS noise.
   * Allow the bias while testing stability separately against the anchor. */
  APP_GYRO_STILL_RATE_RAW = 1024, /* 16 dps, includes initial sensor bias */
  APP_GYRO_STILL_RATE_DELTA_RAW = 64, /* 1 dps relative to window anchor */
  APP_GYRO_STILL_ACCEL_DELTA_RAW = 80 /* 0.0195 g relative to window anchor */
};

typedef enum {
  APP_GYRO_CAL_WAITING = 0,
  APP_GYRO_CAL_QUALIFYING = 1,
  APP_GYRO_CAL_SAMPLING = 2,
  APP_GYRO_CAL_COMPLETE = 3
} APP_GyroCalibrationPhase;

typedef struct {
  /* Once accepted, sums are replaced by Q20 bias; they are never needed together. */
  int32_t sum_or_bias[3];
  uint32_t last_ms;
  int16_t accel_anchor[3];
  int16_t gyro_anchor[3];
  uint8_t count; /* collected calibration samples; zero during stillness dwell */
  uint8_t phase;
  uint16_t still_elapsed_ms;
} APP_GyroCalibration;

void APP_GyroCalibrationBreak(APP_GyroCalibration *state);
void APP_GyroCalibrationUpdate(APP_GyroCalibration *state, uint32_t now_ms,
                               const int16_t accel[3], const int16_t gyro[3]);

#endif
