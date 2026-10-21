// Copyright (c) Jacdac Organization.
// Licensed under the MIT license.

#include "jd_services.h"
#include "interfaces/jd_sensor_api.h"
#include "jacdac/dist/c/gyroscope.h"

struct srv_state {
    SENSOR_COMMON;
    union {
        jd_gyroscope_rotation_rates_t sample;
        int32_t sample_arr[3];
    };
    uint32_t nextSample;
};

extern uint8_t gyroscope_pending;

__attribute__((weak)) void gyroscope_data_transform(int32_t sample[3]) {
    // default to same as accel
    accelerometer_data_transform(sample);
}

#if PY32_QMI8656_GYRO_FORCE_STREAMING
static void gyroscope_force_streaming(srv_t *state) {
    state->got_query = 1;
    if (state->streaming_interval == 0)
        state->streaming_interval = PY32_QMI8656_GYRO_STREAMING_INTERVAL_MS;
    if (state->streaming_samples < 4)
        state->streaming_samples = 255;
}
#endif

/* Refresh for both streaming and direct GET. A NULL driver reading invalidates
 * the cache immediately; the common sensor helper otherwise latches READY. */
static bool gyroscope_refresh(srv_t *state) {
    sensor_process(state);
    void *tmp = sensor_get_reading(state);
    if (!tmp) {
        if (state->got_reading) {
            state->got_reading = 0;
            sensor_send_status(state);
        }
        return false;
    }
    memcpy(state->sample_arr, tmp, sizeof(state->sample_arr));
    gyroscope_data_transform(state->sample_arr);
    return true;
}

void gyroscope_process(srv_t *state) {
#ifdef PIN_ACC_INT
    if (!gyroscope_pending && state->jd_inited)
        return;
    gyroscope_pending = 0;
#else
    if (!jd_should_sample(&state->nextSample, 9500))
        return;
#endif

#if PY32_QMI8656_GYRO_FORCE_STREAMING
    gyroscope_force_streaming(state);
#endif

    if (gyroscope_refresh(state) && sensor_should_stream(state))
        jd_send(state->service_index, JD_GET(JD_REG_READING), &state->sample, sizeof(state->sample));
}

void gyroscope_handle_packet(srv_t *state, jd_packet_t *pkt) {
    /* Checking status must also refresh validity, even between process ticks. */
    if (pkt->service_command == JD_GET(JD_REG_READING) ||
        pkt->service_command == JD_GET(JD_REG_STATUS_CODE)) {
        if (pkt->service_command == JD_GET(JD_REG_READING))
            state->got_query = 1;
        if (!gyroscope_refresh(state) && pkt->service_command == JD_GET(JD_REG_READING))
            return; /* no reading report until a fresh measurement is available */
    }
    sensor_handle_packet_simple(state, pkt, &state->sample, sizeof(state->sample));
}

SRV_DEF(gyroscope, JD_SERVICE_CLASS_GYROSCOPE);

void gyroscope_init(const gyroscope_api_t *api) {
    SRV_ALLOC(gyroscope);
    state->api = api;
}
