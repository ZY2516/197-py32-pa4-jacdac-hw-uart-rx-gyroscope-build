#include "gyro_calibration.h"

static int outside(int32_t value, int32_t limit)
{
  return value < -limit || value > limit;
}

void APP_GyroCalibrationBreak(APP_GyroCalibration *state)
{
  if (state->phase != APP_GYRO_CAL_COMPLETE) {
    state->count = 0;
    state->phase = APP_GYRO_CAL_WAITING;
    state->still_elapsed_ms = 0;
  }
}

void APP_GyroCalibrationUpdate(APP_GyroCalibration *state, uint32_t now_ms,
                               const int16_t accel[3], const int16_t gyro[3])
{
  uint32_t norm = 0;
  uint32_t gap = 0;
  unsigned i;

  if (state->phase == APP_GYRO_CAL_COMPLETE)
    return;

  if (state->phase != APP_GYRO_CAL_WAITING) {
    gap = now_ms - state->last_ms;
    if (gap > APP_GYRO_MAX_GAP_MS)
      APP_GyroCalibrationBreak(state);
    else if (gap < APP_GYRO_SAMPLE_MS)
      return; /* duplicate polls cannot shorten calibration */
  }

  for (i = 0; i < 3; ++i) {
    int32_t a = accel[i];
    norm += (uint32_t)(a * a); /* sum fits uint32_t even for three INT16_MINs */
    if (outside(gyro[i], APP_GYRO_STILL_RATE_RAW)) {
      APP_GyroCalibrationBreak(state);
      return;
    }
  }
  /* Squared magnitude in [0.9g, 1.1g]; independent of resting orientation. */
  if (norm < 13589545u || norm > 20300431u) {
    APP_GyroCalibrationBreak(state);
    return;
  }

  if (state->phase != APP_GYRO_CAL_WAITING) {
    for (i = 0; i < 3; ++i) {
      if (outside((int32_t)accel[i] - state->accel_anchor[i], APP_GYRO_STILL_ACCEL_DELTA_RAW) ||
          outside((int32_t)gyro[i] - state->gyro_anchor[i], APP_GYRO_STILL_RATE_DELTA_RAW)) {
        APP_GyroCalibrationBreak(state);
        return;
      }
    }
  } else {
    for (i = 0; i < 3; ++i) {
      state->accel_anchor[i] = accel[i];
      state->gyro_anchor[i] = gyro[i];
    }
    state->phase = APP_GYRO_CAL_QUALIFYING;
    state->last_ms = now_ms;
    return;
  }

  state->last_ms = now_ms;
  if (state->phase == APP_GYRO_CAL_QUALIFYING) {
    /* Each qualified gap is 20..60ms, so this uint16_t sum cannot overflow.
     * Count elapsed time rather than assuming a fixed acquisition cadence. */
    state->still_elapsed_ms = (uint16_t)(state->still_elapsed_ms + gap);
    if (state->still_elapsed_ms < APP_GYRO_STILL_DWELL_MS)
      return;
    state->phase = APP_GYRO_CAL_SAMPLING;
    state->count = 0;
    for (i = 0; i < 3; ++i)
      state->sum_or_bias[i] = 0;
  }

  for (i = 0; i < 3; ++i)
    state->sum_or_bias[i] += gyro[i];
  if (++state->count == APP_GYRO_STILL_SAMPLES) {
    for (i = 0; i < 3; ++i)
      state->sum_or_bias[i] = state->sum_or_bias[i] * 16384 / APP_GYRO_STILL_SAMPLES;
    state->phase = APP_GYRO_CAL_COMPLETE;
  }
}
