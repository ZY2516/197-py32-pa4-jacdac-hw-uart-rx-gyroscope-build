# 197 PY32 PA4 Jacdac Hardware UART RX Gyroscope Build

This project is copied from:

`00-reference-docs\claude\194-py32-pa4-jacdac-hw-uart-rx-hybrid-build-3\194-py32-pa4-jacdac-hw-uart-rx-hybrid-build`

It is staged under:

`D:\py32-jacdac-rx-staging\PY32-DEMO-1\197-py32-pa4-jacdac-hw-uart-rx-gyroscope-build`

## Goal

Preserve the build-194 PA4/USART1 hardware RX and GPIO bit-bang TX while
publishing the build-36 QMI8656 gyroscope profile from service index 1.

Rationale: the chip has no 48MHz option and log 123 showed the 24MHz software
polling loop cannot resolve edges faster than ~40 ticks against a 24-tick bit
time — the software bit-bang RX route is structurally out of margin. PA4 has a
USART1_RX alternate function (RX only), enabling the hybrid PHY.

## Implementation

Changed from the selected build-194 baseline:

- `Src/py32_qmi8656_jacdac_services.c`: the two firmware-version strings are
  set to the exact build-36 value `0.2.1-py32-gyro-beacon`.

Unchanged:

- PA4 AF1 / USART1 RXNE hardware receive path and FE/NE/ORE handling
- EXTI4_15 ownership, TIM14 timebase, and GPIO bit-bang TX timing
- Jacdac runtime, protocol diagnostics, CRC statistics, and RX rings
- RGB/status behavior including the fast green connected blink
- PB3/PB4 QMI8656 initialization and probe implementation
- Complete `third_party\jacdac-c` tree and Keil project source list

## Expected Evidence

Minimum useful result:

- `g_app_jd_rx_hw_rxne_count` increases (AF index correct);
- `rx_complete` increases with end codes 4/5 instead of 1.

Success result:

- `crc_ok` increases and tracks `rx_complete`.

See `197-protected-baseline-sha256.md` and
`197-gyroscope-integration-progress-log.md` for traceability and validation.
