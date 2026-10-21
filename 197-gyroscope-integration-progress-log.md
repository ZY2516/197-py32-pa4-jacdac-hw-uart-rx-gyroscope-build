# 197 Gyroscope Integration Progress Log

## Scope

- Parent: selected build-194 `-3` hardware-UART-RX hybrid project.
- Preserve PA4 AF1/USART1 RX, EXTI4_15, TIM14, GPIO TX, Jacdac diagnostics,
  RGB/status behavior, and PB3/PB4 QMI8656 support.
- Keep exactly one application service at index 1: gyroscope.

## Source Change

Only `Src\py32_qmi8656_jacdac_services.c` changed. Both exported firmware
version strings now use `0.2.1-py32-gyro-beacon`. Product ID, service class,
sampling, bias calibration, coordinate transform, streaming configuration, and
connected blink are unchanged.

## Static Verification

- Pre-change assertion failed as expected with `wanted=0 legacy=2`.
- Post-change assertion passed with `wanted=2 legacy=0`.
- Protected inputs and the complete `third_party\jacdac-c` tree match the
  selected baseline; see `197-protected-baseline-sha256.md`.

## Build, Flash, and Hardware Verification

### Keil Rebuild

- Rebuild time: `2026-07-20 16:52:22` to `16:52:31` Asia/Shanghai.
- Compiler: ARMCC `V5.06 update 7 (build 960)`.
- Result: `0 Error(s), 81 Warning(s)`.
- Program size: `Code=22864 RO-data=696 RW-data=340 ZI-data=2724`.
- Total ROM size: `23576 (0x5C18)`, below `0x6000`.
- `RW_IRAM1`: `0xBF8`, below `0xC00` with 8 bytes remaining.
- `gyroscope_init` and `gyroscope_process` are live in the MAP.
- `accelerometer_init` and `accelerometer_process` are removed by the linker.
- No Jacdac heap reduction was required.

### Artifact SHA-256

- AXF: `3B207DA02F82C7BEAAA38C067A1765FA085A6659BEED3A7EB0155B673F453D03`
- HEX: `9744084686764AAFA64A8AEA9B9616476DE394F3B0D241BA3F40844838C63692`
- MAP: `F12CF9B5B3022443F0C87E7E379983BA9A76CD6F48221CFC1B180EC0DE01A683`

### Flash and Hardware

Pending detection of the required ST-Link probe
`006000133900001456314D43`, followed by fresh host, SWD, and logic-analyzer
validation.
