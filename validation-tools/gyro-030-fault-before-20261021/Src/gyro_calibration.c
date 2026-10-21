#include "gyro_calibration.h"

static int outside(int32_t value, int32_t limit)
{
  return value < -limit || value > limit;
}

void APP_GyroCalibrationBreak(APP_GyroCalibration *state)
{
  if (state->count < APP_GYRO_STILL_SAMPLES)
    state->count = 0;
}

void APP_GyroCalibrationUpdate(APP_GyroCalibration *state, uint32_t now_ms,
                               const int16_t accel[3], const int16_t gyro[3])
{
  uint32_t norm = 0;
  unsigned i;

  if (state->count == APP_GYRO_STILL_SAMPLES)
    return;

  if (state->count) {
    uint32_t gap = now_ms - state->last_ms;
    if (gap > APP_GYRO_MAX_GAP_MS)
      state->count = 0;
    else if (gap < APP_GYRO_SAMPLE_MS)
      return; /* duplicate polls cannot shorten calibration */
  }

  for (i = 0; i < 3; ++i) {
    int32_t a = accel[i];
    norm += (uint32_t)(a * a); /* sum fits uint32_t even for three INT16_MINs */
    if (outside(gyro[i], APP_GYRO_STILL_RATE_RAW)) {
      state->count = 0;
      return;
    }
  }
  /* Squared magnitude in [0.9g, 1.1g]; independent of resting orientation. */
  if (norm < 13589545u || norm > 20300431u) {
    state->count = 0;
    return;
  }

  if (state->count) {
    for (i = 0; i < 3; ++i) {
      if (outside((int32_t)accel[i] - state->accel_anchor[i], APP_GYRO_STILL_ACCEL_DELTA_RAW) ||
          outside((int32_t)gyro[i] - state->gyro_anchor[i], APP_GYRO_STILL_RATE_DELTA_RAW)) {
        state->count = 0;
        return;
      }
    }
  } else {
    for (i = 0; i < 3; ++i) {
      state->sum_or_bias[i] = 0;
      state->accel_anchor[i] = accel[i];
      state->gyro_anchor[i] = gyro[i];
    }
  }

  for (i = 0; i < 3; ++i)
    state->sum_or_bias[i] += gyro[i];
  state->last_ms = now_ms;
  if (++state->count == APP_GYRO_STILL_SAMPLES) {
    for (i = 0; i < 3; ++i)
      state->sum_or_bias[i] = state->sum_or_bias[i] * 16384 / APP_GYRO_STILL_SAMPLES;
  }
}
