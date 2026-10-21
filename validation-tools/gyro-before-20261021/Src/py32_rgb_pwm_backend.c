#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "interfaces/jd_pins.h"
#include "interfaces/jd_pwm.h"
#include "py32_rgb_pwm_backend.h"

typedef struct
{
  uint8_t id;
  GPIO_TypeDef *port;
  uint16_t pin;
  uint32_t cc_enable;
  volatile uint32_t *ccr;
} APP_RgbPwmChannel;

static uint8_t g_tim1_pwm_initialized;
static uint32_t g_tim1_pwm_period;

static APP_RgbPwmChannel APP_RgbPwmGetChannel(uint8_t id)
{
  APP_RgbPwmChannel ch;

  ch.id = id;
  ch.port = GPIOA;
  ch.pin = GPIO_PIN_5;
  ch.cc_enable = TIM_CCER_CC1E;
  ch.ccr = &TIM1->CCR1;

  switch (id)
  {
  case PY32_RGB_PIN_R:
    break;
  case PY32_RGB_PIN_G:
    ch.port = GPIOA;
    ch.pin = GPIO_PIN_7;
    ch.cc_enable = TIM_CCER_CC4E;
    ch.ccr = &TIM1->CCR4;
    break;
  case PY32_RGB_PIN_B:
    ch.port = GPIOB;
    ch.pin = GPIO_PIN_5;
    ch.cc_enable = TIM_CCER_CC3E;
    ch.ccr = &TIM1->CCR3;
    break;
  default:
    ch.id = 0u;
    ch.port = 0;
    ch.pin = 0u;
    ch.cc_enable = 0u;
    ch.ccr = 0;
    break;
  }

  return ch;
}

static void APP_RgbPwmConfigureGpio(const APP_RgbPwmChannel *ch, uint32_t mode)
{
  GPIO_InitTypeDef gpio;

  if ((ch == 0) || (ch->port == 0))
  {
    return;
  }

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  gpio.Pin = ch->pin;
  gpio.Mode = mode;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF2_TIM1;
  HAL_GPIO_Init(ch->port, &gpio);
}

static void APP_RgbPwmConfigureTim1(uint32_t period, uint8_t prescaler)
{
  uint32_t psc;

  if (period == 0u)
  {
    period = PY32_RGB_PWM_PERIOD;
  }

  if (prescaler == 0u)
  {
    prescaler = 1u;
  }

  psc = (uint32_t)prescaler - 1u;

  if ((g_tim1_pwm_initialized != 0u) && (g_tim1_pwm_period == period))
  {
    return;
  }

  RCC->APBENR2 |= RCC_APBENR2_TIM1EN;

  TIM1->CR1 = 0u;
  TIM1->PSC = psc;
  TIM1->ARR = period - 1u;
  TIM1->CNT = 0u;

  TIM1->CCR1 = 0u;
  TIM1->CCR3 = 0u;
  TIM1->CCR4 = 0u;

  TIM1->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M);
  TIM1->CCMR1 |= TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1;

  TIM1->CCMR2 &= ~(TIM_CCMR2_CC3S | TIM_CCMR2_OC3M | TIM_CCMR2_CC4S | TIM_CCMR2_OC4M);
  TIM1->CCMR2 |= TIM_CCMR2_OC3PE | TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 |
                 TIM_CCMR2_OC4PE | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1;

  TIM1->CCER &= ~(TIM_CCER_CC1E | TIM_CCER_CC3E | TIM_CCER_CC4E);
  TIM1->BDTR |= TIM_BDTR_MOE;
  TIM1->EGR = TIM_EGR_UG;
  TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

  g_tim1_pwm_period = period;
  g_tim1_pwm_initialized = 1u;
}

void pin_set(int pin, int v)
{
  APP_RgbPwmChannel ch = APP_RgbPwmGetChannel((uint8_t)pin);

  if (ch.port == 0)
  {
    return;
  }

  HAL_GPIO_WritePin(ch.port, ch.pin, v ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t jd_pwm_init(uint8_t pin, uint32_t period, uint32_t duty, uint8_t prescaler)
{
  APP_RgbPwmChannel ch = APP_RgbPwmGetChannel(pin);

  if (ch.port == 0)
  {
    return 0u;
  }

  APP_RgbPwmConfigureTim1(period, prescaler);
  APP_RgbPwmConfigureGpio(&ch, GPIO_MODE_AF_PP);
  jd_pwm_set_duty(pin, duty);
  jd_pwm_enable(pin, false);

  return pin;
}

void jd_pwm_set_duty(uint8_t pwm_id, uint32_t duty)
{
  APP_RgbPwmChannel ch = APP_RgbPwmGetChannel(pwm_id);

  if ((ch.ccr == 0) || (g_tim1_pwm_period == 0u))
  {
    return;
  }

  if (duty > g_tim1_pwm_period)
  {
    duty = g_tim1_pwm_period;
  }

  *ch.ccr = duty;
}

void jd_pwm_enable(uint8_t pwm_id, bool enabled)
{
  APP_RgbPwmChannel ch = APP_RgbPwmGetChannel(pwm_id);

  if (ch.port == 0)
  {
    return;
  }

  if (enabled)
  {
    APP_RgbPwmConfigureGpio(&ch, GPIO_MODE_AF_PP);
    TIM1->CCER |= ch.cc_enable;
  }
  else
  {
    TIM1->CCER &= ~ch.cc_enable;
    APP_RgbPwmConfigureGpio(&ch, GPIO_MODE_OUTPUT_PP);
  }
}

void pwr_enter_tim(void)
{
}

void pwr_leave_tim(void)
{
}

void pwr_enter_no_sleep(void)
{
}

void pwr_leave_no_sleep(void)
{
}
