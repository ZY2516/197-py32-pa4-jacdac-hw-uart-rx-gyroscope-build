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

## Current gyroscope behavior (0.3.2)

The device description shown by the host is `Jacdac Gyroscope Sensor v1.0`.
The firmware version is `0.3.2-py32-gyro-beacon`; the product identifier
remains `0x32516D56`.

The firmware first requires 600ms of continuous stillness, then collects 81
calibration samples (at least another 1.6 seconds) to estimate initial bias.
Motion or a failed/late sample cancels either stage and restarts the full
600ms qualification. Acceleration magnitude, acceleration
stability, and angular-rate stability must all qualify. Until completion, measured
angular velocity is published without bias correction. After calibration,
the bias stays fixed until reboot so later slow rotation is preserved.
Initial calibration permits up to 16 degrees/s of raw bias and requires
window stability within 1 degree/s, together with the acceleration checks.
These gates account for the measured production sample's ~7.5 degrees/s
initial offset and 0.12-0.16 degrees/s RMS noise.

The published service remains three-axis angular velocity at index 1, with
the original 12-byte Q20 payload and `(-Y, -X, -Z)` transform. No attitude
calculation has been added. Failed acquisitions stop reading reports;
configuration and recovery advance in scheduled steps without retry sleeps.
I2C transactions allow a bounded STOP/BUSY settling interval before the next
transfer and use the original 20ms transaction timeout. This fixes the 0.3.0
regression that repeatedly restarted configuration without producing samples.

See [GYROSCOPE-CALIBRATION.md](GYROSCOPE-CALIBRATION.md) for thresholds,
limitations, tests, and sample acceptance steps. The Keil project includes
the new `Src/gyro_calibration.c` source; reopen the project if it was already
open before this update.

## Historical build-197 baseline

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

## Keil build

Open `MDK-ARM/Project.uvprojx` in Keil uVision 5 and use ARM Compiler 5.06.
The project targets `PY32F002Bx5`; install its Puya device pack and ARM CMSIS
support in Keil. HAL, BSP, and CMSIS driver headers from the official firmware
release `1.2.1` are included under `third_party/py32f002b-firmware`, so no SDK
directory outside this project is required.

For a command-line rebuild without flashing:

```cmd
build_and_flash.cmd --build-only
```

The script finds `uVision.com` in common Keil installation directories,
including `D:\Keil_v5`. For another installation, set the `UVISION`
environment variable to its full path. Running the script without
`--build-only` also flashes the configured ST-Link target.

If the project was already open when its dependency paths were repaired,
close and reopen the project to reload the configuration, then run
**Rebuild all target files**.
