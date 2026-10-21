# 194 Hardware UART RX Hybrid Progress Log

## Hypothesis

The 24MHz software bit-bang RX path is structurally short on margin: log 123
measured a minimum software edge resolution of ~40 ticks against a 24-tick bit
time, and the two open-loop constants (`source5` anchor `0x04e2`, next-start
rewind `0x0000`) were never validated by any SWD-evidenced run. Two board facts
close the remaining unknowns from the 193 analysis:

1. This chip does **not** support 48MHz HSI — the dormant
   `APP_ENABLE_48MHZ_CLOCK` path is dead and stays at `0U`.
2. PA4 **does** have USART1_RX alternate function (RX only, no TX) — so the
   RX leg can move to the USART peripheral while TX stays GPIO bit-bang.

Moving byte sampling into USART hardware widens the timing budget from ~1us
per bit to ~10us per byte and removes all three empirical RX constants
(anchor, rewind, sample advance); per-byte start-bit sync is done by the
peripheral. Architecture follows jacdac-stm32x0/stm32/uart.c (official
Cortex-M0 single-wire reference) with two deviations: no DMA on PY32F002B, so
RXNE is polled synchronously inside the existing IRQ-off frame window; and no
TX AF on PA4, so TX keeps the 193 bit-bang path (including the 12us tail low,
which matches the stm32x0 reference).

## Changes vs 193

- `Inc/py32_jacdac_board.h`
  - `APP_JACDAC_RX_USE_HW_UART = 1U` (set to `0U` for one-switch fallback to
    the 193 assembly sampler for A/B comparison).
  - `APP_JACDAC_USART = USART1`, clock enable via `RCC->APBENR2`.
  - `APP_JACDAC_USART_RX_AF = 1U` — **provisional**, see Open Items.
  - `APP_JACDAC_USART_BRR_1M = 0x0018` (PCLK 24MHz, oversampling 16,
    USARTDIV = 24/16 = 1.5 → mantissa 1 / fraction 8 → exactly 1Mbaud,
    0% baud error).
- `Src/py32_jacdac_runtime.c`
  - New helpers next to the existing register-direct GPIO code:
    `APP_JacdacHwUartInit` (one-time BRR/CR setup, called from `uart_init_`),
    `APP_JacdacGpioToUartRxAfMode` (EXTI IMR off + clear pending, AFR[0] pin4
    field, MODER to AF — mirrors the TX-claim precedent),
    `APP_JacdacUartRxEnable` (UE|RE, then SR/DR read to flush stale flags),
    `APP_JacdacUartRxDisableToInput` (CR1=0, then existing
    `APP_JacdacGpioToInputMode` which restores input + EXTI).
  - New `APP_JacdacReceiveFrameSyncHwUart`: synchronous RXNE polling loop
    inside `target_disable_irq()`, first-byte timeout = 220us (existing
    `APP_JACDAC_RX_START_TIMEOUT_US`), inter-byte timeout = 16us (existing
    `APP_JACDAC_RX_INTERBYTE_TIMEOUT_US`) rebased on each RXNE tick. FE/NE/ORE
    on a byte aborts the frame. Existing timing diagnostics are kept:
    byte_done_ticks = RXNE tick, start_ticks back-computed by 10 bit times.
  - `APP_JacdacReceiveFrameSync` dispatches to the new path when
    `APP_JACDAC_RX_USE_HW_UART != 0`; the whole 193 anchor/reanchor/assembly
    path is retained unchanged as the fallback branch.
  - New diagnostics: `g_app_jd_rx_hw_rxne_count`, `g_app_jd_rx_hw_fe_count`,
    `g_app_jd_rx_hw_ne_count`, `g_app_jd_rx_hw_ore_count`,
    `g_app_jd_rx_hw_first_byte_wait_ticks`.
  - New end/source codes: end_code 6 = USART error flag abort;
    first_start_source 6 = hardware UART path (source 3 kept for
    "no byte received" like 193's start timeout).
- `MDK-ARM/py32_jacdac_rx_fastpath.s`: untouched (live fallback).
- Unchanged: TX bit-bang path, EXTI lead-pulse detection, `uart_wait_high`
  software poll (the stm32x0 reference keeps it too — USART cannot recognise
  the Jacdac lead pulse), all of `third_party/jacdac-c`, NVIC setup (the
  synchronous polling design adds no interrupt).

## Verification (to run on hardware)

1. Keil rebuild — expect `0 Error(s)`; warnings on par with 193 (the retained
   dead fallback branch may add unreachable-code notes). gcc `-fsyntax-only`
   against the real HAL/CMSIS headers already passes on the modified sources.
2. Flash via `build_and_flash.cmd`, attach SWD with normal Jacdac host
   traffic on the bus, observe after ~15s:
   - `g_app_jd_rx_hw_rxne_count` increasing → AF index is correct.
   - `g_app_jd_diag_crc_ok` increasing and tracking
     `g_app_jd_diag_rx_complete` → goal met.
   - `g_app_jd_rx_timing_bytes[0..7]` match host-sent frame bytes.
   - `g_app_jd_rx_hw_fe/ne/ore_count` zero or near-zero.
   - `g_app_jd_rx_hw_first_byte_wait_ticks` plausible (order of the old
     lead-to-data gap, roughly 0x400-0x600 ticks).
3. Regression: device announce frames (TX path) still received by the host.
4. A/B if needed: `APP_JACDAC_RX_USE_HW_UART = 0U` reproduces 193 behaviour.

## Open items / decision points

- **AF index for PA4→USART1_RX is unverified** (datasheet AF table not in the
  provided materials; official examples only show PB4/PB5 with AF1).
  Decision rule: if `g_app_jd_rx_hw_rxne_count` stays 0 while
  `rx_complete` grows and every frame ends with end_code 1, set
  `APP_JACDAC_USART_RX_AF = 3U` and rebuild. Best to confirm once against the
  real datasheet pin AF table before first flash.
- If FE counts are high with a correct AF, capture
  `g_app_jd_rx_timing_byte_done_ticks` deltas to check whether the host's
  inter-byte cadence violates the 16us timeout assumption.

## RGB status LED enabled (2026-07-15)

The board RGB status LED (common anode: R=PA5, G=PA7, B=PB5) was already
plumbed — `py32_rgb_pwm_backend.c` drives TIM1 CH1/CH4/CH3 via AF2, and
`jd_status_light.c` runs with `JD_CONFIG_STATUS 1` — but the channel
multipliers in `jd_user_config.h` were debug leftovers (`R=0, G=2, B=255`),
which disabled red entirely and made green invisible. Pin/AF mapping now
verified against the PY32F002B datasheet AF table (PA5 AF2=TIM1_CH1,
PA7 AF2=TIM1_CH4, PB5 AF2=TIM1_CH3); the same table confirms PA4 AF1=USART_RX,
closing the "AF index unverified" open item above.

Changes:

- `Inc/jd_user_config.h`: `RGB_LED_PERIOD 600`, `LED_R_MULT 250`,
  `LED_G_MULT 76`, `LED_B_MULT 221` — the official jacdac-module-template
  reference calibration (~50 lux per channel).
- `third_party/jacdac-c/services/jd_status_light.c`: restored the official
  `fast_blink` glow entry `GLOW(0, 250, 250)` (removed the PY32 cadence hack).
- `Src/py32_qmi8656_jacdac_services.c`: `jd_connected_blink =
  JD_BLINK(FAST, 1, GREEN)` so the connected heartbeat is a clear ~8ms green
  blink per announce instead of the default FAINT (sub-ms) pulse.
- `Inc/py32_rgb_pwm_backend.h`: fallback period synced to 600; dropped unused
  `PY32_RGB_ACTIVE_HIGH`.
- No startup-blink addition needed: the vendored `jd_protocol.c` already ends
  `jd_init()` with `jd_blink(JD_BLINK_STARTUP)` (official behaviour).

Expected behaviour: blue blink at power-on; red slow breathing while no
brain/host is on the bus; a short green blink roughly every 500ms once
connected; 7 fast blue blinks on the `identify` command. TIM1 is otherwise
unused (Jacdac timing runs on TIM14), so no timer conflict; PWM is
24MHz/600 = 40kHz. Flash cost is config-only (no new code paths).

## Build-3: gyro service restore + multi-device RX guard + dim blinks (2026-07-17)

Driven by A/B against `36-py32-qmi8656-jacdac-tim14-phy-project-only-20260717`
(known-good TIM14 bit-bang PHY build). Three user-reported issues.

1. Wrong output content (FACT, service layer): 194 lineage carried the
   accelerometer diagnostic profile (raw accel, identity transform, PID
   0x3c7b02df) while build 36 is the product profile: **gyroscope** service,
   32-sample startup bias calibration, (-y,-x,-z) axis transform, PID
   0x32516D56, name "PY32 Jacdac Gyroscope Beacon". PHY/driver layers proved
   content-identical (qmi8656_probe.c byte-equal; jd_physical/jd_control
   diffs are diag-only). Fix: ported build-36 service file verbatim into
   `Src/py32_qmi8656_jacdac_services.c` (kept the jd_connected_blink line),
   fw version now `0.2.30-py32-gyro-rx-hybrid`. Note: bias path introduces
   int64 divide-by-32; if IROM 0x6000 overflows, replace both `/ 32` with
   arithmetic `>> 5`.

2. Multi-device disconnects (HYPOTHESIS with code evidence): hybrid RX aborts
   instantly on FE/NE/ORE and on 16us RXNE silence; unlike the 36 sampler
   (which only ends on true line silence), an abort can happen mid-foreign-
   frame, after which jd_physical may launch our TX lead ~150us later — a
   single-instant bus-high probe in ClaimBusForTxLow can hit a stop-bit high
   of the still-active frame and collide. Fix: post-loop **bus-quiet drain**
   in `APP_JacdacReceiveFrameSyncHwUart` (hold RX, discard stragglers, until
   line high for continuous 22us; guard-bounded). New SWD counters:
   `g_app_jd_rx_hw_quiet_rearm_count`, `g_app_jd_rx_hw_quiet_guard_abort_count`.
   Also restored `APP_JACDAC_STARTUP_ANNOUNCE_COUNT` 0 -> 8 (parity with 36).
   NOT changed (needs on-bus verification): 16us interbyte timeout, 220us
   first-byte timeout, EXTI dispatch without lead-width validation.

3. Status LED brightness: blink (event) layer was hardcoded full-scale
   0xff00 in jd_status_light.c; now `JD_STATUS_BLINK_LEVEL` (default 0xff00)
   and board sets **0x2000 (1/8)** in jd_user_config.h — one step above the
   ~3% glow layer, which stays the dimmest level. Single knob to retune.

Expected multi-device observables after flash: `bus_uart_error` and FE count
may still grow (foreign-frame tails read as garbage) but host-side device
drops should stop; quiet_rearm_count > 0 confirms the drain is absorbing
frame tails that previously triggered mid-frame TX.
