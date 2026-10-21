#ifndef PY32_RGB_PWM_BACKEND_H
#define PY32_RGB_PWM_BACKEND_H

#include <stdint.h>

enum
{
  PY32_RGB_PIN_R = 1u,
  PY32_RGB_PIN_G = 2u,
  PY32_RGB_PIN_B = 3u,
  /* Fallback only; jd_status_init() passes RGB_LED_PERIOD (600). */
  PY32_RGB_PWM_PERIOD = 600u
};

#endif
