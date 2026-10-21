#include "py32_jacdac_runtime.h"

#include <string.h>

#include "main.h"
#include "py32_jacdac_board.h"
#include "py32f002bx5.h"

#include "jacdac/dist/c/accelerometer.h"
#include "jacdac/dist/c/control.h"
#include "jacdac/dist/c/gyroscope.h"
#include "jd_protocol.h"
#include "jd_io.h"
#include "jd_service_framework.h"
#include "jd_util.h"
#include "interfaces/jd_alloc.h"
#include "interfaces/jd_app.h"
#include "interfaces/jd_hw.h"
#include "interfaces/jd_rx.h"
#include "interfaces/jd_tx.h"

enum
{
  APP_JACDAC_HEAP_BYTES = 896u,
  APP_JACDAC_TX_LEAD_LOW_US = 16u,
  APP_JACDAC_TX_LEAD_TO_DATA_US = 52u,
  APP_JACDAC_RX_LEAD_TO_DATA_US = 57u,
  APP_JACDAC_TX_TAIL_LOW_US = 12u,
  APP_JACDAC_UART_BIT_US = 1u,
  APP_JACDAC_RX_BIT_TICK_PAD = 0u,
  APP_JACDAC_TX_BIT_TICK_PAD = 1u,
  APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 21u,
  APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x04e2u,
  APP_JACDAC_RX_NEXT_START_REWIND_TICKS = 0x0000u,
  APP_JACDAC_WAIT_HIGH_TIMEOUT_US = 1000u,
  APP_JACDAC_RX_START_TIMEOUT_US = 220u,
  APP_JACDAC_RX_INTERBYTE_TIMEOUT_US = 16u,
  APP_JACDAC_RX_ENTRY_LOW_REANCHOR_HIGH_TIMEOUT_US = 48u,
  APP_JACDAC_RX_ENTRY_LOW_REANCHOR_START_TIMEOUT_US = 48u,
  APP_JACDAC_RX_STOP_HIGH_TIMEOUT_US = 3u,
  APP_JACDAC_RX_BYTE_CADENCE_EXTRA_TICKS = 3u,
  APP_JACDAC_RX_USE_10BIT_CADENCE = 0u,
  APP_JACDAC_RX_REQUIRE_STOP_HIGH = 0u,
  APP_JACDAC_RX_MAX_FRAME_BYTES = 252u,
  APP_JACDAC_START_PULSE_MIN_US = 2u,
  APP_JACDAC_START_PULSE_MAX_US = 220u,
  APP_JACDAC_STARTUP_ANNOUNCE_DELAY_US = 20000u,
  APP_JACDAC_STARTUP_ANNOUNCE_INTERVAL_US = 150000u,
  APP_JACDAC_STARTUP_ANNOUNCE_COUNT = 8u,
  /* Multi-device guard: after the RXNE loop ends, keep RX ownership until
   * the bus has stayed high for this continuous window (longest legal
   * in-frame high run is ~11us: 0xFF byte plus stop bit), so a framing-error
   * abort mid-frame cannot hand "bus idle" to the TX scheduler while a
   * foreign frame is still on the wire. */
  APP_JACDAC_RX_BUS_QUIET_US = 22u,
  /* Each guard increment is one busy-poll iteration observed low; bounds the
   * IRQ-off drain to a few ms even if the line is stuck low. */
  APP_JACDAC_RX_BUS_QUIET_GUARD_LOOPS = 8192u,
  APP_JACDAC_RX_TIMING_SLOTS = 8u,
  APP_JACDAC_RX_PHASE_DIAG_BYTES = 8u,
  APP_JACDAC_RX_PHASE_DIAG_EDGE_SLOTS = 24u,
  APP_JACDAC_RX_END_CODE_PHASE_DIAG = 9u
};

typedef struct
{
  cb_t callback;
  uint64_t due_us;
  uint8_t armed;
} APP_JacdacTimerState;

typedef struct
{
  volatile uint32_t *idr;              /* 0 */
  volatile uint32_t *tim_cnt;          /* 4 */
  uint32_t pin_mask;                   /* 8 */
  uint8_t *buffer;                     /* 12 */
  volatile uint16_t *start_ticks;      /* 16 */
  volatile uint16_t *byte_done_ticks;  /* 20 */
  uint32_t maxbytes;                   /* 24 */
  uint16_t start_tick;                 /* 28 */
  uint16_t bit_ticks;                  /* 30 */
  uint16_t sample_offset_ticks;        /* 32 */
  uint16_t interbyte_timeout_ticks;    /* 34 */
  uint8_t timing_slots;                /* 36 */
  uint8_t end_code;                    /* 37 */
  uint16_t received;                   /* 38 */
  uint16_t next_start_rewind_ticks;    /* 40 */
} APP_JacdacRxMultiAsmState;

extern int APP_JacdacRxMultiByteAsm(APP_JacdacRxMultiAsmState *state);

static APP_JacdacTimerState g_timer_state;
static volatile uint32_t g_irq_lock_depth;
static uint32_t g_irq_saved_primask;
static volatile uint8_t g_runtime_ready;
static volatile uint8_t g_line_falling_pending;
static volatile uint8_t g_line_falling_captured;
static volatile uint8_t g_rx_complete_pending;
static volatile uint8_t g_tx_complete_pending;
static volatile uint8_t g_uart_rx_active;
static volatile uint8_t g_uart_tx_active;
static uint8_t g_jacdac_tick_timer_ready;
static uint8_t g_app_jd_ticks_per_us_cached;
static uint8_t g_app_jd_rx_bit_ticks_cached;
static uint8_t g_app_jd_tx_bit_ticks_cached;
static uint8_t g_heap[APP_JACDAC_HEAP_BYTES];
static uint32_t g_heap_used;
static int g_rx_complete_data_left;
static uint64_t g_startup_announce_due_us;
static uint8_t g_startup_announce_remaining;

static uint8_t APP_JacdacReadBusPinFast(void);
static uint16_t APP_JacdacUsToTicks(uint32_t us);
static uint16_t APP_JacdacRxBitTicks(void);
static uint16_t APP_JacdacTxBitTicks(void);
uint16_t app_jacdac_diag_tick_now(void);
static int APP_JacdacWaitForBusLevelFast(uint8_t expected_high, uint32_t timeout_us, uint16_t *observed_tick);
static int APP_JacdacWaitForRxStartLowTicks(uint16_t timeout_ticks, uint16_t *observed_tick);
static int APP_JacdacWaitForBusHighTicks(uint16_t timeout_ticks, uint16_t *observed_tick);
static int APP_JacdacWaitForEntryLowReanchor(uint16_t high_timeout_ticks, uint16_t start_timeout_ticks,
                                             uint16_t *start_tick);
static void APP_JacdacRxTimingBeginFrame(uint16_t rx_bit_ticks, uint16_t sample_offset_ticks,
                                         uint16_t start_timeout_ticks, uint16_t interbyte_timeout_ticks);
static void APP_JacdacRxTimingPublishMultiByte(const uint8_t *buffer, uint32_t received);
static void APP_JacdacRxTimingFinish(uint32_t received, uint32_t expected_frame_bytes, int data_left,
                                     uint32_t end_code);
static void APP_JacdacRxAnchorPublish(uint16_t entry_tick, uint8_t entry_low, uint16_t first_start_tick,
                                      uint8_t first_start_source);
static void APP_JacdacRxPhaseDiagnosticCapture(uint16_t lead_rise_tick, uint16_t start_tick, uint16_t bit_ticks);
static void APP_JacdacWriteBusPin(GPIO_PinState state);
static uint32_t APP_JacdacProbeAndSetOutputModeIfBusHigh(uint32_t mode_mask, uint32_t output_mode);
static void APP_JacdacLatchPendingFallIfBusLow(void);
static void APP_JacdacPumpDeferredCallbacks(void);
static void APP_JacdacPumpLeadPulseHint(void);
static void APP_JacdacHwUartInit(void);
static void APP_JacdacGpioToUartRxAfMode(void);
static void APP_JacdacUartRxEnable(void);
static void APP_JacdacUartRxDisableToInput(void);
static int APP_JacdacReceiveFrameSyncHwUart(uint8_t *buffer, uint32_t maxbytes, int *data_left);
static int APP_JacdacClaimBusForTxLow(void);
static int APP_JacdacSendFrameOnBus(const uint8_t *bytes, uint32_t numbytes, uint8_t report_tx_completion);
void APP_MotionReadingsUpdate(void);

#if JD_CONFIG_STATUS != 1
uint8_t jd_connected_blink = JD_BLINK_CONNECTED;
#endif
uint32_t now = 0;
uint8_t cpu_mhz = 24;
volatile uint32_t g_app_jd_diag_rx_complete;
volatile uint32_t g_app_jd_diag_crc_ok;
volatile uint32_t g_app_jd_diag_control_packet;
volatile uint32_t g_app_jd_diag_product_id_get;
volatile uint32_t g_app_jd_rx_first_reanchor_used;
volatile uint32_t g_app_jd_rx_timing_seq;
volatile uint32_t g_app_jd_rx_timing_frame_count;
volatile uint32_t g_app_jd_rx_timing_lead_count;
volatile uint32_t g_app_jd_rx_timing_last_received;
volatile uint32_t g_app_jd_rx_timing_last_expected_frame_bytes;
volatile int32_t g_app_jd_rx_timing_last_data_left;
volatile uint32_t g_app_jd_rx_timing_end_code;
volatile uint32_t g_app_jd_rx_timing_ticks_per_us;
volatile uint16_t g_app_jd_rx_timing_rx_bit_ticks;
volatile uint16_t g_app_jd_rx_timing_sample_offset_ticks;
volatile uint16_t g_app_jd_rx_timing_start_timeout_ticks;
volatile uint16_t g_app_jd_rx_timing_interbyte_timeout_ticks;
volatile uint16_t g_app_jd_rx_timing_lead_low_ticks;
volatile uint16_t g_app_jd_rx_timing_lead_rise_tick;
volatile uint16_t g_app_jd_rx_timing_first_start_delay_ticks;
volatile uint8_t g_app_jd_rx_timing_start_count;
volatile uint32_t g_app_jd_rx_timing_stop_checked_mask;
volatile uint32_t g_app_jd_rx_timing_stop_high_mask;
volatile uint16_t g_app_jd_rx_timing_start_ticks[APP_JACDAC_RX_TIMING_SLOTS];
volatile uint16_t g_app_jd_rx_timing_start_deltas[APP_JACDAC_RX_TIMING_SLOTS];
volatile uint16_t g_app_jd_rx_timing_byte_done_ticks[APP_JACDAC_RX_TIMING_SLOTS];
volatile uint16_t g_app_jd_rx_timing_done_to_next_deltas[APP_JACDAC_RX_TIMING_SLOTS];
volatile uint8_t g_app_jd_rx_timing_bytes[APP_JACDAC_RX_TIMING_SLOTS];
volatile uint32_t g_app_jd_rx_timing_next_start_found_mask;

volatile uint32_t g_app_jd_rx_anchor_seq;
volatile uint16_t g_app_jd_rx_anchor_entry_tick;
volatile uint16_t g_app_jd_rx_anchor_lead_rise_tick;
volatile uint16_t g_app_jd_rx_anchor_lead_to_entry_ticks;
volatile uint16_t g_app_jd_rx_anchor_first_start_tick;
volatile uint16_t g_app_jd_rx_anchor_entry_to_first_start_ticks;
volatile uint16_t g_app_jd_rx_anchor_lead_to_first_start_ticks;
volatile uint8_t g_app_jd_rx_anchor_entry_low;
volatile uint8_t g_app_jd_rx_anchor_first_start_source;
volatile uint32_t g_app_jd_line_fall_pending_set_count;
volatile uint32_t g_app_jd_line_fall_pending_overridden_by_direct_count;
volatile uint32_t g_app_jd_line_fall_direct_dispatch_count;
volatile uint32_t g_app_jd_line_fall_deferred_dispatch_count;
volatile uint32_t g_app_jd_line_fall_deferred_bus_high_count;
volatile uint16_t g_app_jd_line_fall_handler_entry_tick;
volatile uint16_t g_app_jd_line_fall_after_wait_high_tick;
volatile uint16_t g_app_jd_uart_start_rx_entry_tick;
volatile uint16_t g_app_jd_receive_frame_entry_tick;
volatile uint8_t g_app_jd_rx_phase_diag_pending;
volatile uint8_t g_app_jd_rx_phase_diag_done;
volatile uint8_t g_app_jd_rx_phase_diag_edge_count;
volatile uint16_t g_app_jd_rx_phase_diag_start_delay;
volatile uint16_t g_app_jd_rx_phase_diag_edges[APP_JACDAC_RX_PHASE_DIAG_EDGE_SLOTS];
volatile uint32_t g_app_jd_rx_hw_rxne_count;
volatile uint32_t g_app_jd_rx_hw_fe_count;
volatile uint32_t g_app_jd_rx_hw_ne_count;
volatile uint32_t g_app_jd_rx_hw_ore_count;
volatile uint16_t g_app_jd_rx_hw_first_byte_wait_ticks;
volatile uint32_t g_app_jd_rx_hw_quiet_rearm_count;
volatile uint32_t g_app_jd_rx_hw_quiet_guard_abort_count;

#define APP_JACDAC_TICK_NOW() ((uint16_t)TIM14->CNT)
#define APP_JACDAC_BUS_IS_HIGH() (((APP_JACDAC_GPIO_PORT->IDR & APP_JACDAC_GPIO_PIN) != 0u) ? 1u : 0u)
#define APP_JACDAC_BUS_DRIVE_HIGH() (APP_JACDAC_GPIO_PORT->BSRR = APP_JACDAC_GPIO_PIN)
#define APP_JACDAC_BUS_DRIVE_LOW() (APP_JACDAC_GPIO_PORT->BSRR = (uint32_t)APP_JACDAC_GPIO_PIN << 16u)
#define APP_JACDAC_GPIO_MODE_MASK 0x300u
#define APP_JACDAC_GPIO_OUTPUT_MODE 0x100u
#define APP_JACDAC_WAIT_UNTIL(deadline)                         \
  do                                                            \
  {                                                             \
    while ((int16_t)(APP_JACDAC_TICK_NOW() - (deadline)) < 0) \
    {                                                           \
    }                                                           \
  } while (0)

static void APP_JacdacGpioToInputMode(void)
{
  uint32_t moder = APP_JACDAC_GPIO_PORT->MODER;
  uint32_t pupdr = APP_JACDAC_GPIO_PORT->PUPDR;
  uint32_t ospeedr = APP_JACDAC_GPIO_PORT->OSPEEDR;

  APP_JacdacWriteBusPin(GPIO_PIN_SET);

  moder &= ~APP_JACDAC_GPIO_MODE_MASK;
  pupdr &= ~APP_JACDAC_GPIO_MODE_MASK;
  pupdr |= APP_JACDAC_GPIO_OUTPUT_MODE;
  ospeedr |= APP_JACDAC_GPIO_MODE_MASK;

  APP_JACDAC_GPIO_PORT->OTYPER &= ~APP_JACDAC_GPIO_PIN;
  APP_JACDAC_GPIO_PORT->OSPEEDR = ospeedr;
  APP_JACDAC_GPIO_PORT->PUPDR = pupdr;
  APP_JACDAC_GPIO_PORT->MODER = moder;

  __HAL_GPIO_EXTI_CLEAR_IT(APP_JACDAC_GPIO_PIN);
  EXTI->IMR |= APP_JACDAC_GPIO_PIN;
}

#define APP_JACDAC_GPIO_AF_MODE 0x200u
#define APP_JACDAC_GPIO_PIN4_AFR_MASK (0xFu << 16u)
#define APP_JACDAC_GPIO_PIN4_AFR_VALUE ((uint32_t)APP_JACDAC_USART_RX_AF << 16u)

static void APP_JacdacHwUartInit(void)
{
  APP_JACDAC_USART_CLK_ENABLE();
  APP_JACDAC_USART->CR1 = 0u;
  APP_JACDAC_USART->CR2 = 0u;
  APP_JACDAC_USART->CR3 = 0u;
  APP_JACDAC_USART->BRR = APP_JACDAC_USART_BRR_1M;
}

static void APP_JacdacGpioToUartRxAfMode(void)
{
  uint32_t moder = APP_JACDAC_GPIO_PORT->MODER;
  uint32_t afr = APP_JACDAC_GPIO_PORT->AFR[0];

  EXTI->IMR &= ~APP_JACDAC_GPIO_PIN;
  __HAL_GPIO_EXTI_CLEAR_IT(APP_JACDAC_GPIO_PIN);

  afr &= ~APP_JACDAC_GPIO_PIN4_AFR_MASK;
  afr |= APP_JACDAC_GPIO_PIN4_AFR_VALUE;
  APP_JACDAC_GPIO_PORT->AFR[0] = afr;

  moder &= ~APP_JACDAC_GPIO_MODE_MASK;
  moder |= APP_JACDAC_GPIO_AF_MODE;
  APP_JACDAC_GPIO_PORT->MODER = moder;
}

static void APP_JacdacUartRxEnable(void)
{
  volatile uint32_t discard;

  APP_JACDAC_USART->CR1 = USART_CR1_UE | USART_CR1_RE;
  discard = APP_JACDAC_USART->SR;
  discard = APP_JACDAC_USART->DR;
  (void)discard;
}

static void APP_JacdacUartRxDisableToInput(void)
{
  APP_JACDAC_USART->CR1 = 0u;
  APP_JacdacGpioToInputMode();
}

static uint32_t APP_JacdacTicksPerUs(void)
{
  uint32_t ticks = g_app_jd_ticks_per_us_cached;
  if (ticks == 0u)
  {
    ticks = SystemCoreClock / 1000000u;
    if (ticks == 0u)
    {
      ticks = 1u;
    }
  }

  return ticks;
}

static uint16_t APP_JacdacUsToTicks(uint32_t us)
{
  uint32_t ticks = APP_JacdacTicksPerUs() * us;
  if (ticks == 0u)
  {
    ticks = 1u;
  }

  return (uint16_t)ticks;
}

static uint16_t APP_JacdacRxBitTicks(void)
{
  uint16_t ticks = g_app_jd_rx_bit_ticks_cached;

  if (ticks == 0u)
  {
    ticks = (uint16_t)(APP_JacdacUsToTicks(APP_JACDAC_UART_BIT_US) + APP_JACDAC_RX_BIT_TICK_PAD);
  }

  return ticks;
}

uint16_t app_jacdac_diag_tick_now(void)
{
  return APP_JACDAC_TICK_NOW();
}

static uint16_t APP_JacdacTxBitTicks(void)
{
  uint16_t ticks = g_app_jd_tx_bit_ticks_cached;

  if (ticks == 0u)
  {
    ticks = (uint16_t)(APP_JacdacUsToTicks(APP_JACDAC_UART_BIT_US) + APP_JACDAC_TX_BIT_TICK_PAD);
  }

  return ticks;
}

static void APP_JacdacWaitCoreTicks(uint32_t core_ticks)
{
  uint32_t prev = SysTick->VAL;
  const uint32_t reload = SysTick->LOAD + 1u;
  uint32_t elapsed = 0u;

  while (elapsed < core_ticks)
  {
    uint32_t current = SysTick->VAL;

    if (prev >= current)
    {
      elapsed += prev - current;
    }
    else
    {
      elapsed += prev + (reload - current);
    }

    prev = current;
  }
}

static void APP_JacdacWaitUsRaw(uint32_t us)
{
  APP_JacdacWaitCoreTicks(APP_JacdacTicksPerUs() * us);
}

static uint64_t APP_JacdacGetMicros(void)
{
  uint32_t ms_a;
  uint32_t ms_b;
  uint32_t val;
  const uint32_t reload = SysTick->LOAD + 1u;

  do
  {
    ms_a = HAL_GetTick();
    val = SysTick->VAL;
    ms_b = HAL_GetTick();
  } while (ms_a != ms_b);

  return (uint64_t)ms_a * 1000u + (uint64_t)((reload - val) / APP_JacdacTicksPerUs());
}

static void APP_JacdacWriteBusPin(GPIO_PinState state)
{
  if (state == GPIO_PIN_SET)
  {
    APP_JACDAC_GPIO_PORT->BSRR = APP_JACDAC_GPIO_PIN;
  }
  else
  {
    APP_JACDAC_GPIO_PORT->BSRR = (uint32_t)APP_JACDAC_GPIO_PIN << 16u;
  }
}

static uint8_t APP_JacdacReadBusPinFast(void)
{
  return (APP_JACDAC_GPIO_PORT->IDR & APP_JACDAC_GPIO_PIN) != 0u ? 1u : 0u;
}

static __attribute__((noinline)) uint32_t APP_JacdacProbeAndSetOutputModeIfBusHigh(uint32_t mode_mask, uint32_t output_mode)
{
  uint32_t moder = APP_JACDAC_GPIO_PORT->MODER;

  if ((APP_JACDAC_GPIO_PORT->IDR & APP_JACDAC_GPIO_PIN) != 0u)
  {
    moder &= ~mode_mask;
    moder |= output_mode;
    APP_JACDAC_GPIO_PORT->MODER = moder;
  }

  return APP_JACDAC_GPIO_PORT->MODER;
}

static void APP_JacdacLatchPendingFallIfBusLow(void)
{
  if (APP_JacdacReadBusPinFast() == 0u)
  {
    g_line_falling_pending = 1u;
    g_line_falling_captured = 0u;
  }
}

static void APP_JacdacTickTimerInit(void)
{
  uint32_t ticks_per_us;

  if (g_jacdac_tick_timer_ready != 0u)
  {
    return;
  }

  RCC->APBENR2 |= RCC_APBENR2_TIM14EN;
  TIM14->PSC = 0u;
  TIM14->ARR = 0xFFFFu;
  TIM14->CNT = 0u;
  TIM14->CR1 = TIM_CR1_CEN;

  ticks_per_us = SystemCoreClock / 1000000u;
  if (ticks_per_us == 0u)
  {
    ticks_per_us = 1u;
  }

  g_app_jd_ticks_per_us_cached = (uint8_t)ticks_per_us;
  g_app_jd_rx_bit_ticks_cached = (uint8_t)(ticks_per_us * APP_JACDAC_UART_BIT_US + APP_JACDAC_RX_BIT_TICK_PAD);
  g_app_jd_tx_bit_ticks_cached = (uint8_t)(ticks_per_us * APP_JACDAC_UART_BIT_US + APP_JACDAC_TX_BIT_TICK_PAD);

  g_jacdac_tick_timer_ready = 1u;
}

static int APP_JacdacWaitForBusLevelFast(uint8_t expected_high, uint32_t timeout_us, uint16_t *observed_tick)
{
  const uint16_t timeout_ticks = APP_JacdacUsToTicks(timeout_us);
  const uint16_t deadline = (uint16_t)(APP_JACDAC_TICK_NOW() + timeout_ticks);

  while ((int16_t)(APP_JACDAC_TICK_NOW() - deadline) < 0)
  {
    uint16_t now_ticks = APP_JACDAC_TICK_NOW();

    if (APP_JACDAC_BUS_IS_HIGH() == expected_high)
    {
      if (observed_tick != 0)
      {
        *observed_tick = now_ticks;
      }
      return 0;
    }
  }

  return -1;
}

static int APP_JacdacWaitForRxStartLowTicks(uint16_t timeout_ticks, uint16_t *observed_tick)
{
  const uint16_t deadline = (uint16_t)(APP_JACDAC_TICK_NOW() + timeout_ticks);
  volatile uint32_t *const idr = &APP_JACDAC_GPIO_PORT->IDR;
  const uint32_t pin_mask = APP_JACDAC_GPIO_PIN;

  do
  {
    if ((*idr & pin_mask) == 0u)
    {
      if (observed_tick != 0)
      {
        *observed_tick = APP_JACDAC_TICK_NOW();
      }
      return 0;
    }
  } while ((int16_t)(APP_JACDAC_TICK_NOW() - deadline) < 0);

  return -1;
}

static int APP_JacdacWaitForBusHighTicks(uint16_t timeout_ticks, uint16_t *observed_tick)
{
  const uint16_t deadline = (uint16_t)(APP_JACDAC_TICK_NOW() + timeout_ticks);
  volatile uint32_t *const idr = &APP_JACDAC_GPIO_PORT->IDR;
  const uint32_t pin_mask = APP_JACDAC_GPIO_PIN;

  do
  {
    if ((*idr & pin_mask) != 0u)
    {
      if (observed_tick != 0)
      {
        *observed_tick = APP_JACDAC_TICK_NOW();
      }
      return 0;
    }
  } while ((int16_t)(APP_JACDAC_TICK_NOW() - deadline) < 0);

  return -1;
}

static int APP_JacdacWaitForEntryLowReanchor(uint16_t high_timeout_ticks, uint16_t start_timeout_ticks,
                                             uint16_t *start_tick)
{
  uint16_t high_tick = 0u;

  if (start_tick == 0)
  {
    return -1;
  }

  if (APP_JacdacWaitForBusHighTicks(high_timeout_ticks, &high_tick) < 0)
  {
    return -1;
  }

  if (APP_JacdacWaitForRxStartLowTicks(start_timeout_ticks, start_tick) < 0)
  {
    return -1;
  }

  return 0;
}

static void APP_JacdacRxTimingBeginFrame(uint16_t rx_bit_ticks, uint16_t sample_offset_ticks,
                                         uint16_t start_timeout_ticks, uint16_t interbyte_timeout_ticks)
{
  uint32_t i;

  g_app_jd_rx_timing_seq++;
  g_app_jd_rx_timing_frame_count++;
  g_app_jd_rx_timing_last_received = 0u;
  g_app_jd_rx_timing_last_expected_frame_bytes = 0u;
  g_app_jd_rx_timing_last_data_left = 0;
  g_app_jd_rx_timing_end_code = 0u;
  g_app_jd_rx_timing_ticks_per_us = APP_JacdacTicksPerUs();
  g_app_jd_rx_timing_rx_bit_ticks = rx_bit_ticks;
  g_app_jd_rx_timing_sample_offset_ticks = sample_offset_ticks;
  g_app_jd_rx_timing_start_timeout_ticks = start_timeout_ticks;
  g_app_jd_rx_timing_interbyte_timeout_ticks = interbyte_timeout_ticks;
  g_app_jd_rx_timing_first_start_delay_ticks = 0xffffu;
  g_app_jd_rx_timing_start_count = 0u;
  g_app_jd_rx_timing_stop_checked_mask = 0u;
  g_app_jd_rx_timing_stop_high_mask = 0u;
  g_app_jd_rx_timing_next_start_found_mask = 0u;

  for (i = 0u; i < APP_JACDAC_RX_TIMING_SLOTS; ++i)
  {
    g_app_jd_rx_timing_start_ticks[i] = 0u;
    g_app_jd_rx_timing_start_deltas[i] = 0u;
    g_app_jd_rx_timing_byte_done_ticks[i] = 0u;
    g_app_jd_rx_timing_done_to_next_deltas[i] = 0u;
    g_app_jd_rx_timing_bytes[i] = 0u;
  }
}

static void APP_JacdacRxTimingPublishMultiByte(const uint8_t *buffer, uint32_t received)
{
  uint32_t i;
  uint32_t count = received;

  if (count > APP_JACDAC_RX_TIMING_SLOTS)
  {
    count = APP_JACDAC_RX_TIMING_SLOTS;
  }

  g_app_jd_rx_timing_start_count = (uint8_t)count;

  for (i = 0u; i < count; ++i)
  {
    g_app_jd_rx_timing_bytes[i] = buffer[i];
    g_app_jd_rx_timing_stop_checked_mask |= (uint32_t)(1u << i);

    if (i == 0u)
    {
      g_app_jd_rx_timing_start_deltas[i] = 0u;
      if (g_app_jd_rx_timing_lead_rise_tick != 0u)
      {
        g_app_jd_rx_timing_first_start_delay_ticks =
            (uint16_t)(g_app_jd_rx_timing_start_ticks[i] - g_app_jd_rx_timing_lead_rise_tick);
      }
    }
    else
    {
      g_app_jd_rx_timing_start_deltas[i] =
          (uint16_t)(g_app_jd_rx_timing_start_ticks[i] - g_app_jd_rx_timing_start_ticks[i - 1u]);
    }

    if ((i + 1u) < count)
    {
      g_app_jd_rx_timing_next_start_found_mask |= (uint32_t)(1u << i);
      g_app_jd_rx_timing_done_to_next_deltas[i] =
          (uint16_t)(g_app_jd_rx_timing_start_ticks[i + 1u] - g_app_jd_rx_timing_byte_done_ticks[i]);
    }
    else
    {
      g_app_jd_rx_timing_done_to_next_deltas[i] = 0xffffu;
    }
  }
}

static void APP_JacdacRxTimingFinish(uint32_t received, uint32_t expected_frame_bytes, int data_left,
                                     uint32_t end_code)
{
  g_app_jd_rx_timing_last_received = received;
  g_app_jd_rx_timing_last_expected_frame_bytes = expected_frame_bytes;
  g_app_jd_rx_timing_last_data_left = data_left;
  g_app_jd_rx_timing_end_code = end_code;
}

static void APP_JacdacRxAnchorPublish(uint16_t entry_tick, uint8_t entry_low, uint16_t first_start_tick,
                                      uint8_t first_start_source)
{
  const uint16_t lead_rise_tick = g_app_jd_rx_timing_lead_rise_tick;

  g_app_jd_rx_anchor_seq++;
  g_app_jd_rx_anchor_entry_tick = entry_tick;
  g_app_jd_rx_anchor_entry_low = entry_low;
  g_app_jd_rx_anchor_first_start_source = first_start_source;
  g_app_jd_rx_anchor_lead_rise_tick = lead_rise_tick;
  g_app_jd_rx_anchor_first_start_tick = first_start_tick;

  if (lead_rise_tick != 0u)
  {
    g_app_jd_rx_anchor_lead_to_entry_ticks = (uint16_t)(entry_tick - lead_rise_tick);
  }
  else
  {
    g_app_jd_rx_anchor_lead_to_entry_ticks = 0xffffu;
  }

  if (first_start_source != 0u && first_start_source != 3u)
  {
    g_app_jd_rx_anchor_entry_to_first_start_ticks = (uint16_t)(first_start_tick - entry_tick);
    if (lead_rise_tick != 0u)
    {
      g_app_jd_rx_anchor_lead_to_first_start_ticks = (uint16_t)(first_start_tick - lead_rise_tick);
    }
    else
    {
      g_app_jd_rx_anchor_lead_to_first_start_ticks = 0xffffu;
    }
  }
  else
  {
    g_app_jd_rx_anchor_entry_to_first_start_ticks = 0xffffu;
    g_app_jd_rx_anchor_lead_to_first_start_ticks = 0xffffu;
  }
}

static void APP_JacdacRxPhaseDiagnosticCapture(uint16_t lead_rise_tick, uint16_t start_tick, uint16_t bit_ticks)
{
  uint16_t now_tick;
  uint16_t deadline = (uint16_t)(start_tick + (uint16_t)((bit_ticks << 6) + (bit_ticks << 4)));
  uint8_t last_level;
  uint8_t edge_count = 0u;

  APP_JACDAC_WAIT_UNTIL(start_tick);
  now_tick = APP_JACDAC_TICK_NOW();
  last_level = APP_JACDAC_BUS_IS_HIGH();

  while ((int16_t)(now_tick - deadline) < 0)
  {
    uint8_t level;

    level = APP_JACDAC_BUS_IS_HIGH();
    if (level != last_level)
    {
      g_app_jd_rx_phase_diag_edges[edge_count] = (uint16_t)(now_tick - lead_rise_tick);
      edge_count++;
      if (edge_count >= APP_JACDAC_RX_PHASE_DIAG_EDGE_SLOTS)
      {
        break;
      }
      last_level = level;
    }
    now_tick = APP_JACDAC_TICK_NOW();
  }

  g_app_jd_rx_phase_diag_edge_count = edge_count;
  g_app_jd_rx_phase_diag_done = 1u;
}

/* Build 194 hardware RX path: the USART peripheral performs per-byte start-bit
 * synchronisation, replacing the open-loop "source5" anchor prediction and the
 * rewind/sample-advance empirical constants of the 193 bit-bang sampler. Runs
 * with interrupts disabled, mirroring the 193 synchronous frame structure. */
static int APP_JacdacReceiveFrameSyncHwUart(uint8_t *buffer, uint32_t maxbytes, int *data_left)
{
  uint32_t received = 0u;
  uint32_t expected_frame_bytes = 0u;
  uint32_t end_code = 4u;
  uint16_t deadline;
  uint16_t anchor_entry_tick;
  uint8_t anchor_entry_low;
  const uint16_t ticks_per_us = (uint16_t)APP_JacdacTicksPerUs();
  const uint16_t rx_bit_ticks = APP_JacdacRxBitTicks();
  const uint16_t byte_ticks_approx = (uint16_t)(rx_bit_ticks * 10u);
  const uint16_t start_timeout_ticks = (uint16_t)(ticks_per_us * APP_JACDAC_RX_START_TIMEOUT_US);
  const uint16_t interbyte_timeout_ticks = (uint16_t)(ticks_per_us * APP_JACDAC_RX_INTERBYTE_TIMEOUT_US);

  if ((buffer == 0) || (maxbytes == 0u) || (data_left == 0))
  {
    return -1;
  }

  target_disable_irq();
  APP_JacdacRxTimingBeginFrame(rx_bit_ticks, 0u, start_timeout_ticks, interbyte_timeout_ticks);

  anchor_entry_tick = APP_JACDAC_TICK_NOW();
  g_app_jd_receive_frame_entry_tick = anchor_entry_tick;
  anchor_entry_low = (APP_JacdacReadBusPinFast() == 0u) ? 1u : 0u;

  APP_JacdacGpioToUartRxAfMode();
  APP_JacdacUartRxEnable();

  deadline = (uint16_t)(APP_JACDAC_TICK_NOW() + start_timeout_ticks);

  while (received < maxbytes)
  {
    uint32_t sr = APP_JACDAC_USART->SR;

    if ((sr & USART_SR_RXNE) != 0u)
    {
      const uint16_t now_tick = APP_JACDAC_TICK_NOW();
      const uint32_t value = APP_JACDAC_USART->DR;

      g_app_jd_rx_hw_rxne_count++;

      if ((sr & (USART_SR_FE | USART_SR_NE | USART_SR_ORE)) != 0u)
      {
        if ((sr & USART_SR_FE) != 0u)
        {
          g_app_jd_rx_hw_fe_count++;
        }
        if ((sr & USART_SR_NE) != 0u)
        {
          g_app_jd_rx_hw_ne_count++;
        }
        if ((sr & USART_SR_ORE) != 0u)
        {
          g_app_jd_rx_hw_ore_count++;
        }
        end_code = 6u;
        break;
      }

      buffer[received] = (uint8_t)value;
      if (received < APP_JACDAC_RX_TIMING_SLOTS)
      {
        g_app_jd_rx_timing_byte_done_ticks[received] = now_tick;
        /* RXNE fires at the stop bit; back-compute an approximate start tick
         * so the existing start_deltas diagnostics stay meaningful. */
        g_app_jd_rx_timing_start_ticks[received] = (uint16_t)(now_tick - byte_ticks_approx);
      }
      if (received == 0u)
      {
        g_app_jd_rx_hw_first_byte_wait_ticks = (uint16_t)(now_tick - anchor_entry_tick);
      }
      received++;
      deadline = (uint16_t)(now_tick + interbyte_timeout_ticks);
      continue;
    }

    if ((int16_t)(APP_JACDAC_TICK_NOW() - deadline) >= 0)
    {
      end_code = (received == 0u) ? 1u : 4u;
      break;
    }
  }

  if (received >= maxbytes)
  {
    end_code = 5u;
  }

  /* Hold RX until the wire is provably idle: the RXNE loop can exit while a
   * foreign frame is still in flight (framing-error break, interbyte-timeout
   * misjudgement), and jd_physical may schedule our TX lead ~150us after RX
   * completion. The 36/193 bit-bang sampler ended frames only on true line
   * silence; replicate that invariant here. Stray bytes are drained so ORE
   * cannot latch into the next frame. */
  {
    const uint16_t quiet_ticks = (uint16_t)(ticks_per_us * APP_JACDAC_RX_BUS_QUIET_US);
    uint16_t quiet_deadline = (uint16_t)(APP_JACDAC_TICK_NOW() + quiet_ticks);
    uint32_t guard = 0u;

    while ((int16_t)(APP_JACDAC_TICK_NOW() - quiet_deadline) < 0)
    {
      if ((APP_JACDAC_USART->SR & USART_SR_RXNE) != 0u)
      {
        (void)APP_JACDAC_USART->DR;
        g_app_jd_rx_hw_quiet_rearm_count++;
        quiet_deadline = (uint16_t)(APP_JACDAC_TICK_NOW() + quiet_ticks);
      }

      if (APP_JacdacReadBusPinFast() == 0u)
      {
        quiet_deadline = (uint16_t)(APP_JACDAC_TICK_NOW() + quiet_ticks);
        guard++;
        if (guard >= APP_JACDAC_RX_BUS_QUIET_GUARD_LOOPS)
        {
          g_app_jd_rx_hw_quiet_guard_abort_count++;
          break;
        }
      }
    }
  }

  APP_JacdacUartRxDisableToInput();

  APP_JacdacRxTimingPublishMultiByte(buffer, received);

  if (received >= 3u)
  {
    expected_frame_bytes = (uint32_t)((jd_frame_t *)buffer)->size + 12u;
    if (expected_frame_bytes > APP_JACDAC_RX_MAX_FRAME_BYTES)
    {
      expected_frame_bytes = maxbytes;
    }
  }

  *data_left = (int)(maxbytes - received);
  APP_JacdacRxAnchorPublish(anchor_entry_tick, anchor_entry_low,
                            (received != 0u) ? g_app_jd_rx_timing_start_ticks[0] : 0u,
                            (received != 0u) ? 6u : 3u);
  APP_JacdacRxTimingFinish(received, expected_frame_bytes, *data_left, end_code);

  target_enable_irq();
  return 0;
}

static int APP_JacdacReceiveFrameSync(uint8_t *buffer, uint32_t maxbytes, int *data_left)
{
  int result = 0;
  uint32_t received = 0u;
  uint32_t expected_frame_bytes = 0u;
  uint16_t start_tick = 0u;
  uint16_t anchor_entry_tick;
  uint16_t anchor_first_start_tick = 0u;
  uint8_t anchor_entry_low;
  uint8_t anchor_first_start_source = 0u;
  const uint16_t ticks_per_us = APP_JacdacTicksPerUs();
  const uint16_t rx_bit_ticks = APP_JacdacRxBitTicks();
  const uint16_t byte_ticks = (uint16_t)(rx_bit_ticks * 10u + APP_JACDAC_RX_BYTE_CADENCE_EXTRA_TICKS);
  const uint16_t lead_to_data_ticks = APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS;
  const uint16_t start_timeout_ticks = (uint16_t)(ticks_per_us * APP_JACDAC_RX_START_TIMEOUT_US);
  const uint16_t interbyte_timeout_ticks = (uint16_t)(ticks_per_us * APP_JACDAC_RX_INTERBYTE_TIMEOUT_US);
  const uint16_t reanchor_high_timeout_ticks =
      (uint16_t)(ticks_per_us * APP_JACDAC_RX_ENTRY_LOW_REANCHOR_HIGH_TIMEOUT_US);
  const uint16_t reanchor_start_timeout_ticks =
      (uint16_t)(ticks_per_us * APP_JACDAC_RX_ENTRY_LOW_REANCHOR_START_TIMEOUT_US);
  const uint16_t stop_high_timeout_ticks = (uint16_t)(ticks_per_us * APP_JACDAC_RX_STOP_HIGH_TIMEOUT_US);
  const uint16_t lead_rise_tick = g_app_jd_rx_timing_lead_rise_tick;
  uint16_t sample_offset_ticks = (uint16_t)(rx_bit_ticks + (rx_bit_ticks / 2u));

  if ((buffer == 0) || (maxbytes == 0u) || (data_left == 0))
  {
    return -1;
  }

  if (APP_JACDAC_RX_USE_HW_UART != 0u)
  {
    return APP_JacdacReceiveFrameSyncHwUart(buffer, maxbytes, data_left);
  }

  if (sample_offset_ticks > APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS)
  {
    sample_offset_ticks = (uint16_t)(sample_offset_ticks - APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS);
  }
  else
  {
    sample_offset_ticks = 1u;
  }

  target_disable_irq();
  APP_JacdacRxTimingBeginFrame(rx_bit_ticks, sample_offset_ticks, start_timeout_ticks, interbyte_timeout_ticks);

  anchor_entry_tick = APP_JACDAC_TICK_NOW();
  g_app_jd_receive_frame_entry_tick = anchor_entry_tick;
  anchor_entry_low = (APP_JacdacReadBusPinFast() == 0u) ? 1u : 0u;

  if (lead_rise_tick != 0u)
  {
    uint16_t lead_anchor_tick = (uint16_t)(lead_rise_tick + lead_to_data_ticks);

    if ((int16_t)(anchor_entry_tick - lead_anchor_tick) < 0)
    {
      start_tick = lead_anchor_tick;
      anchor_first_start_tick = start_tick;
      anchor_first_start_source = 5u;
    }
  }

  if (anchor_first_start_source == 0u && anchor_entry_low != 0u)
  {
    if (APP_JacdacWaitForEntryLowReanchor(reanchor_high_timeout_ticks, reanchor_start_timeout_ticks, &start_tick) ==
        0)
    {
      anchor_first_start_source = 4u;
      g_app_jd_rx_first_reanchor_used++;
    }
    else
    {
      start_tick = APP_JACDAC_TICK_NOW();
      anchor_first_start_source = 1u;
    }

    anchor_first_start_tick = start_tick;
  }
  else if (anchor_first_start_source == 0u && APP_JacdacWaitForRxStartLowTicks(start_timeout_ticks, &start_tick) < 0)
  {
    *data_left = (int)maxbytes;
    APP_JacdacRxAnchorPublish(anchor_entry_tick, anchor_entry_low, 0u, 3u);
    APP_JacdacRxTimingFinish(received, expected_frame_bytes, *data_left, 1u);
    goto exit;
  }
  else if (anchor_first_start_source == 0u)
  {
    anchor_first_start_tick = start_tick;
    anchor_first_start_source = 2u;
  }

  if (g_app_jd_rx_phase_diag_pending != 0u && anchor_first_start_source == 5u && lead_rise_tick != 0u)
  {
    g_app_jd_rx_phase_diag_pending = 0u;
    APP_JacdacRxPhaseDiagnosticCapture(lead_rise_tick, start_tick, rx_bit_ticks);
    *data_left = (int)maxbytes;
    g_app_jd_rx_timing_last_data_left = *data_left;
    g_app_jd_rx_timing_end_code = APP_JACDAC_RX_END_CODE_PHASE_DIAG;
    goto exit;
  }

  {
    APP_JacdacRxMultiAsmState asm_state;

    (void)byte_ticks;
    (void)stop_high_timeout_ticks;

    asm_state.idr = &APP_JACDAC_GPIO_PORT->IDR;
    asm_state.tim_cnt = &TIM14->CNT;
    asm_state.pin_mask = APP_JACDAC_GPIO_PIN;
    asm_state.buffer = buffer;
    asm_state.start_ticks = g_app_jd_rx_timing_start_ticks;
    asm_state.byte_done_ticks = g_app_jd_rx_timing_byte_done_ticks;
    asm_state.maxbytes = maxbytes;
    asm_state.start_tick = start_tick;
    asm_state.bit_ticks = rx_bit_ticks;
    asm_state.sample_offset_ticks = sample_offset_ticks;
    asm_state.interbyte_timeout_ticks = interbyte_timeout_ticks;
    asm_state.timing_slots = APP_JACDAC_RX_TIMING_SLOTS;
    asm_state.end_code = 0u;
    asm_state.received = 0u;
    asm_state.next_start_rewind_ticks = APP_JACDAC_RX_NEXT_START_REWIND_TICKS;

    if (APP_JacdacRxMultiByteAsm(&asm_state) < 0)
    {
      *data_left = (int)maxbytes;
      APP_JacdacRxAnchorPublish(anchor_entry_tick, anchor_entry_low, anchor_first_start_tick,
                                anchor_first_start_source);
      APP_JacdacRxTimingFinish(0u, expected_frame_bytes, *data_left, 2u);
      goto exit;
    }

    received = asm_state.received;
    APP_JacdacRxTimingPublishMultiByte(buffer, received);

    if (received >= 3u)
    {
      expected_frame_bytes = (uint32_t)((jd_frame_t *)buffer)->size + 12u;
      if (expected_frame_bytes > APP_JACDAC_RX_MAX_FRAME_BYTES)
      {
        expected_frame_bytes = maxbytes;
      }
    }

    *data_left = (int)(maxbytes - received);
    APP_JacdacRxAnchorPublish(anchor_entry_tick, anchor_entry_low, anchor_first_start_tick,
                              anchor_first_start_source);
    APP_JacdacRxTimingFinish(received, expected_frame_bytes, *data_left, asm_state.end_code);
  }

exit:
  target_enable_irq();
  return result;
}

static void APP_JacdacPumpTimer(void)
{
  cb_t callback = 0;

  target_disable_irq();

  if (g_timer_state.armed && g_timer_state.callback != 0 && APP_JacdacGetMicros() >= g_timer_state.due_us)
  {
    callback = g_timer_state.callback;
    g_timer_state.callback = 0;
    g_timer_state.due_us = 0;
    g_timer_state.armed = 0u;
  }

  target_enable_irq();

  if (callback != 0)
  {
    callback();
  }
}

static void APP_JacdacPumpLeadPulseHint(void)
{
  uint8_t bus_high;

  if (g_runtime_ready == 0u || g_uart_tx_active != 0u || g_uart_rx_active != 0u || g_rx_complete_pending != 0u ||
      g_tx_complete_pending != 0u || g_line_falling_pending == 0u || jd_is_busy())
  {
    return;
  }

  target_disable_irq();

  if (g_runtime_ready == 0u || g_uart_tx_active != 0u || g_uart_rx_active != 0u || g_rx_complete_pending != 0u ||
      g_tx_complete_pending != 0u || g_line_falling_pending == 0u || jd_is_busy())
  {
    target_enable_irq();
    return;
  }

  g_line_falling_pending = 0u;
  g_line_falling_captured = 0u;
  target_enable_irq();

  bus_high = APP_JacdacReadBusPinFast();
  g_app_jd_line_fall_deferred_dispatch_count++;

  if (bus_high != 0u)
  {
    g_app_jd_line_fall_deferred_bus_high_count++;
  }

  jd_line_falling();
}

static void APP_JacdacPumpDeferredCallbacks(void)
{
  for (;;)
  {
    uint8_t should_complete_rx = 0u;
    uint8_t should_complete_tx = 0u;
    int rx_data_left = 0;

    target_disable_irq();

    if (g_rx_complete_pending != 0u)
    {
      should_complete_rx = 1u;
      rx_data_left = g_rx_complete_data_left;
      g_rx_complete_pending = 0u;
    }
    else if (g_tx_complete_pending != 0u)
    {
      should_complete_tx = 1u;
      g_tx_complete_pending = 0u;
    }
    target_enable_irq();

    if (should_complete_rx != 0u)
    {
      jd_rx_completed(rx_data_left);
      g_uart_rx_active = 0u;
      __HAL_GPIO_EXTI_CLEAR_IT(APP_JACDAC_GPIO_PIN);
      EXTI->IMR |= APP_JACDAC_GPIO_PIN;
      continue;
    }

    if (should_complete_tx != 0u)
    {
      jd_tx_completed(0);
      g_uart_tx_active = 0u;
      __HAL_GPIO_EXTI_CLEAR_IT(APP_JACDAC_GPIO_PIN);
      EXTI->IMR |= APP_JACDAC_GPIO_PIN;
      continue;
    }

    break;
  }
}

static __attribute__((noinline)) int APP_JacdacClaimBusForTxLow(void)
{
  uint32_t pupdr = APP_JACDAC_GPIO_PORT->PUPDR;
  uint32_t ospeedr = APP_JACDAC_GPIO_PORT->OSPEEDR;

  EXTI->IMR &= ~APP_JACDAC_GPIO_PIN;
  __HAL_GPIO_EXTI_CLEAR_IT(APP_JACDAC_GPIO_PIN);

  pupdr &= ~APP_JACDAC_GPIO_MODE_MASK;
  pupdr |= APP_JACDAC_GPIO_OUTPUT_MODE;
  ospeedr &= ~APP_JACDAC_GPIO_MODE_MASK;
  ospeedr |= APP_JACDAC_GPIO_MODE_MASK;

  APP_JACDAC_GPIO_PORT->OTYPER &= ~APP_JACDAC_GPIO_PIN;
  APP_JACDAC_GPIO_PORT->OSPEEDR = ospeedr;
  APP_JACDAC_GPIO_PORT->PUPDR = pupdr;
  APP_JACDAC_BUS_DRIVE_LOW();

  if ((APP_JacdacProbeAndSetOutputModeIfBusHigh(APP_JACDAC_GPIO_MODE_MASK, APP_JACDAC_GPIO_OUTPUT_MODE) &
       APP_JACDAC_GPIO_MODE_MASK) != APP_JACDAC_GPIO_OUTPUT_MODE)
  {
    APP_JacdacGpioToInputMode();
    return -1;
  }

  return 0;
}

static int APP_JacdacSendFrameOnBus(const uint8_t *bytes, uint32_t numbytes, uint8_t report_tx_completion)
{
  const uint16_t lead_low_ticks = APP_JacdacUsToTicks(APP_JACDAC_TX_LEAD_LOW_US);
  const uint16_t lead_to_data_ticks = APP_JacdacUsToTicks(APP_JACDAC_TX_LEAD_TO_DATA_US);
  const uint16_t tail_low_ticks = APP_JacdacUsToTicks(APP_JACDAC_TX_TAIL_LOW_US);
  const uint16_t bit_ticks = APP_JacdacTxBitTicks();
  volatile uint32_t *const bsrr = &APP_JACDAC_GPIO_PORT->BSRR;
  const uint32_t high_word = APP_JACDAC_GPIO_PIN;
  const uint32_t low_word = (uint32_t)APP_JACDAC_GPIO_PIN << 16u;
  uint16_t deadline;
  uint32_t index;

  if (bytes == 0 || numbytes == 0u || g_uart_rx_active || g_uart_tx_active)
  {
    return -1;
  }

  target_disable_irq();
  g_uart_tx_active = 1u;
  g_line_falling_pending = 0u;

  if (APP_JacdacClaimBusForTxLow() < 0)
  {
    g_uart_tx_active = 0u;
    APP_JacdacLatchPendingFallIfBusLow();
    target_enable_irq();
    return -1;
  }

  deadline = APP_JACDAC_TICK_NOW();
  deadline = (uint16_t)(deadline + lead_low_ticks);
  APP_JACDAC_WAIT_UNTIL(deadline);
  *bsrr = high_word;
  deadline = (uint16_t)(deadline + lead_to_data_ticks);
  APP_JACDAC_WAIT_UNTIL(deadline);

  for (index = 0u; index < numbytes; ++index)
  {
    uint8_t value = bytes[index];

    *bsrr = low_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);

    *bsrr = (value & 0x01u) != 0u ? high_word : low_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);

    *bsrr = (value & 0x02u) != 0u ? high_word : low_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);

    *bsrr = (value & 0x04u) != 0u ? high_word : low_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);

    *bsrr = (value & 0x08u) != 0u ? high_word : low_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);

    *bsrr = (value & 0x10u) != 0u ? high_word : low_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);

    *bsrr = (value & 0x20u) != 0u ? high_word : low_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);

    *bsrr = (value & 0x40u) != 0u ? high_word : low_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);

    *bsrr = (value & 0x80u) != 0u ? high_word : low_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);

    *bsrr = high_word;
    deadline = (uint16_t)(deadline + bit_ticks);
    APP_JACDAC_WAIT_UNTIL(deadline);
  }

  *bsrr = low_word;
  deadline = (uint16_t)(deadline + tail_low_ticks);
  APP_JACDAC_WAIT_UNTIL(deadline);

  APP_JacdacGpioToInputMode();
  // Do not reinterpret our own TX tail low pulse as a fresh RX lead pulse.
  // After switching PB0 back to input, rely on the next external falling edge / EXTI
  // to start reception, instead of immediately self-latching a pending fall.
  if (report_tx_completion != 0u)
  {
    g_tx_complete_pending = 1u;
  }
  target_enable_irq();

  return 0;
}

void jd_alloc_init(void)
{
  g_heap_used = 0u;
  jd_seed_random((uint32_t)APP_JacdacGetMicros() ^ HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2());
}

void jd_alloc_stack_check(void)
{
}

void *jd_alloc(uint32_t size)
{
  uint32_t aligned_size = (size + 3u) & ~3u;
  void *ptr;

  if (aligned_size == 0u)
  {
    aligned_size = 4u;
  }

  if ((g_heap_used + aligned_size) > APP_JACDAC_HEAP_BYTES)
  {
    JD_PANIC();
  }

  ptr = &g_heap[g_heap_used];
  memset(ptr, 0, aligned_size);
  g_heap_used += aligned_size;
  return ptr;
}

uint32_t jd_available_memory(void)
{
  return APP_JACDAC_HEAP_BYTES - g_heap_used;
}

void jd_free(void *ptr)
{
  (void)ptr;
}

void *jd_alloc_emergency_area(uint32_t size)
{
  if (size > APP_JACDAC_HEAP_BYTES)
  {
    JD_PANIC();
  }

  return g_heap;
}

uint64_t hw_device_id(void)
{
  uint64_t upper = ((uint64_t)HAL_GetUIDw0() << 32) | HAL_GetUIDw1();
  return upper ^ HAL_GetUIDw2();
}

void hw_panic(void)
{
  APP_ErrorHandler();
  while (1)
  {
  }
}

void power_pin_enable(int en)
{
  (void)en;
}

void target_disable_irq(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();

  if (g_irq_lock_depth == 0u)
  {
    g_irq_saved_primask = primask;
  }

  g_irq_lock_depth++;
}

void target_enable_irq(void)
{
  if (g_irq_lock_depth == 0u)
  {
    return;
  }

  g_irq_lock_depth--;

  if (g_irq_lock_depth == 0u && g_irq_saved_primask == 0u)
  {
    __enable_irq();
  }
}

void target_wait_us(uint32_t n)
{
  APP_JacdacWaitUsRaw(n);
}

void target_reset(void)
{
  NVIC_SystemReset();
  for (;;)
  {
  }
}

int target_in_irq(void)
{
  return __get_IPSR() != 0u;
}

void target_standby(uint32_t duration_ms)
{
  HAL_Delay(duration_ms);
}

void tim_init(void)
{
  g_timer_state.callback = 0;
  g_timer_state.due_us = 0;
  g_timer_state.armed = 0u;
  g_line_falling_pending = 0u;
  g_rx_complete_pending = 0u;
  g_tx_complete_pending = 0u;
  g_rx_complete_data_left = 0;
  APP_JacdacTickTimerInit();
}

uint64_t tim_get_micros(void)
{
  return APP_JacdacGetMicros();
}

void tim_set_timer(int delta, cb_t cb)
{
  if (delta < 10)
  {
    delta = 10;
  }

  target_disable_irq();

  g_timer_state.callback = cb;
  g_timer_state.armed = cb != 0;
  g_timer_state.due_us = cb != 0 ? (APP_JacdacGetMicros() + (uint32_t)delta) : 0u;

  target_enable_irq();
}

void uart_init_(void)
{
  APP_JacdacTickTimerInit();
  if (APP_JACDAC_RX_USE_HW_UART != 0u)
  {
    APP_JacdacHwUartInit();
  }
  APP_JacdacGpioToInputMode();
  APP_JacdacWriteBusPin(GPIO_PIN_SET);
  g_line_falling_pending = 0u;
  g_line_falling_captured = 0u;
  g_rx_complete_pending = 0u;
  g_tx_complete_pending = 0u;
  g_rx_complete_data_left = 0;
  g_uart_rx_active = 0u;
  g_uart_tx_active = 0u;
  g_app_jd_rx_phase_diag_pending = 1u;
  g_app_jd_rx_phase_diag_done = 0u;
}

int uart_start_tx(const void *data, uint32_t numbytes)
{
  return APP_JacdacSendFrameOnBus((const uint8_t *)data, numbytes, 1u);
}

void uart_start_rx(void *data, uint32_t maxbytes)
{
  int data_left = (int)maxbytes;

  g_app_jd_uart_start_rx_entry_tick = APP_JACDAC_TICK_NOW();
  g_uart_rx_active = 1u;
  g_line_falling_pending = 0u;
  g_line_falling_captured = 0u;

  EXTI->IMR &= ~APP_JACDAC_GPIO_PIN;
  __HAL_GPIO_EXTI_CLEAR_IT(APP_JACDAC_GPIO_PIN);
  target_disable_irq();
  (void)APP_JacdacReceiveFrameSync((uint8_t *)data, maxbytes, &data_left);
  __HAL_GPIO_EXTI_CLEAR_IT(APP_JACDAC_GPIO_PIN);
  g_rx_complete_data_left = data_left;
  g_rx_complete_pending = 1u;
  target_enable_irq();
}

void uart_disable(void)
{
  g_uart_rx_active = 0u;
  g_uart_tx_active = 0u;
  g_line_falling_pending = 0u;
  g_line_falling_captured = 0u;
  g_rx_complete_pending = 0u;
  g_tx_complete_pending = 0u;
  g_rx_complete_data_left = 0;
  __HAL_GPIO_EXTI_CLEAR_IT(APP_JACDAC_GPIO_PIN);
  APP_JacdacGpioToInputMode();
  APP_JacdacLatchPendingFallIfBusLow();
}

int uart_wait_high(void)
{
  const uint16_t low_start_tick = APP_JACDAC_TICK_NOW();
  uint16_t rise_tick = 0u;
  int res = APP_JacdacWaitForBusLevelFast(1u, APP_JACDAC_WAIT_HIGH_TIMEOUT_US, &rise_tick);

  if (res == 0)
  {
    g_app_jd_rx_timing_lead_count++;
    g_app_jd_rx_timing_lead_low_ticks = (uint16_t)(rise_tick - low_start_tick);
    g_app_jd_rx_timing_lead_rise_tick = rise_tick;
  }

  return res;
}

void uart_flush_rx(void)
{
}

void jd_dmesg(const char *format, ...)
{
  (void)format;
}

void jd_app_handle_packet(jd_packet_t *pkt)
{
  (void)pkt;
}

#if JD_CONFIG_STATUS != 1
int jd_status_handle_packet(jd_packet_t *pkt)
{
  (void)pkt;
  return 0;
}

void jd_status_process(void)
{
}

bool jd_status_has_color(void)
{
  return false;
}

void jd_blink(uint8_t encoded)
{
  (void)encoded;
}

void jd_glow(uint32_t glow)
{
  (void)glow;
}
#endif

void app_process(void)
{
  if (g_runtime_ready == 0u)
  {
    return;
  }

  APP_MotionReadingsUpdate();

  if (g_startup_announce_remaining == 0u)
  {
    return;
  }

  if (APP_JacdacGetMicros() < g_startup_announce_due_us)
  {
    return;
  }

  jd_services_announce();
  g_startup_announce_remaining--;
  g_startup_announce_due_us = APP_JacdacGetMicros() + APP_JACDAC_STARTUP_ANNOUNCE_INTERVAL_US;
}

int jd_send_frame(jd_frame_t *frame)
{
  (void)frame;
  return -1;
}

int jd_send_frame_raw(jd_frame_t *frame)
{
  return jd_send_frame(frame);
}

void APP_JacdacRuntimeInit(void)
{
  if (g_runtime_ready)
  {
    return;
  }

  SystemCoreClockUpdate();
  cpu_mhz = (uint8_t)(SystemCoreClock / 1000000u);
  if (cpu_mhz == 0u)
  {
    cpu_mhz = 1u;
  }
  tim_init();
  uart_init_();
  jd_init();
  jd_services_announce();
  g_startup_announce_remaining = APP_JACDAC_STARTUP_ANNOUNCE_COUNT;
  g_startup_announce_due_us = APP_JacdacGetMicros() + APP_JACDAC_STARTUP_ANNOUNCE_DELAY_US;
  g_runtime_ready = 1u;
}

void APP_JacdacRuntimeProcess(void)
{
  if (!g_runtime_ready)
  {
    return;
  }

  APP_JacdacPumpLeadPulseHint();
  APP_JacdacPumpDeferredCallbacks();
  APP_JacdacPumpTimer();
  jd_process_everything();
  APP_JacdacPumpLeadPulseHint();
  APP_JacdacPumpDeferredCallbacks();
  APP_JacdacPumpTimer();
}

void APP_JacdacExtiIrqHandler(void)
{
  if (__HAL_GPIO_EXTI_GET_IT(APP_JACDAC_GPIO_PIN) != RESET)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(APP_JACDAC_GPIO_PIN);

    if (g_uart_tx_active || g_tx_complete_pending != 0u)
    {
      return;
    }

    if (g_runtime_ready && !g_uart_tx_active && !g_uart_rx_active && g_rx_complete_pending == 0u &&
        g_tx_complete_pending == 0u && !jd_is_busy())
    {
      if (APP_JacdacReadBusPinFast() == 0u)
      {
        if (g_line_falling_pending != 0u)
        {
          g_app_jd_line_fall_pending_overridden_by_direct_count++;
        }

        g_line_falling_pending = 0u;
        g_line_falling_captured = 0u;
        g_app_jd_line_fall_direct_dispatch_count++;
        jd_line_falling();
      }
    }
    else
    {
      g_line_falling_pending = 1u;
      g_line_falling_captured = 0u;
      g_app_jd_line_fall_pending_set_count++;
    }
  }
}
