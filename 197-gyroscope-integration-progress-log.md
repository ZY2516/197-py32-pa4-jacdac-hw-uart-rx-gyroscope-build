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

## Local Keil Dependency Repair - 2026-10-21

- The local build failed with 17 errors because its sibling
  `PY32F002B_Firmware_V1.2.1` SDK directory was missing.
- Added 59 required SDK files under `third_party/py32f002b-firmware`.
  All files exactly match Git blobs from the official OpenPuya `1.2.1` tag,
  commit `394627d99085c5eae72142503b0405b84428bb20`; SHA-256 hashes are
  recorded in that directory's `UPSTREAM-SHA256.txt`.
- Updated SDK paths in `Project.uvprojx` and `Project.uvoptx` to use the
  included dependencies. Historical baseline hashes above describe the
  original project configuration, before this path repair.
- Updated `build_and_flash.cmd` to locate the local Keil installation, use
  the included DFP pack for pyOCD, and support `--build-only`.
- Rebuilt using `D:\Keil_v5`, ARMCC 5.06 update 7 build 960, and the locally
  installed Puya DFP 1.2.8: `0 Error(s), 81 Warning(s)`.
- Program size remains `Code=22864 RO-data=696 RW-data=340 ZI-data=2724`.
- The generated HEX is byte-identical to the original successful build:
  `9744084686764AAFA64A8AEA9B9616476DE394F3B0D241BA3F40844838C63692`.
- All 18 application source/header files are unchanged. No flash operation
  was performed. Build output: `MDK-ARM/build-repair-console.log`.
- Original project settings, failing build log, and prior HEX/MAP were
  saved under `validation-tools/build-repair-before-20261021`.

## Stationary-qualified Gyroscope Calibration - 2026-10-21

Implemented firmware `0.3.0-py32-gyro-beacon` for handheld power-up and
manual wiring. The existing service-index-1 angular velocity profile,
12-byte Q20 payload, `(-Y, -X, -Z)` transform, and default 50ms streaming
interval are preserved. No attitude calculation was added.

- Added a hardware-independent, 32-byte calibration state in
  `Inc/gyro_calibration.h` / `Src/gyro_calibration.c`, included in Keil.
  It requires 81 consecutive qualified samples over at least 1.6 seconds,
  using gravity magnitude, acceleration stability, and angular-rate
  magnitude/stability. A motion outlier, failed read, or gap over 60ms
  restarts the candidate. Uncalibrated motion is reported rather than zeroed.
- Initial bias is accepted once and stays fixed until reboot, including
  during motion and communication recovery. No adaptive deadband or Flash
  persistence is introduced. This preserves later slow rotation; very slow
  constant yaw during the initial window remains fundamentally ambiguous.
- Replaced startup/runtime delay-and-retry paths with scheduled
  configuration and address recovery. Config writes and verification are
  split across calls; settling uses a timestamp. Failed acquisition attempts
  are throttled as well. Both raw vectors commit together on full success.
- Removed unused temperature polling and 64-bit fixed-point division.
  Retained the validated range, ODR, filter registers and separate accel/gyro
  read windows. I2C uses a 3ms transaction timeout and a BUSY precheck.
  Periodic register checks detect lost sensor configuration.
- Updated the Jacdac gyro service specifically: invalid readings suppress
  periodic and direct-GET reports, clear its READY latch, and become READY
  again on fresh data. Other sensor service implementations are unchanged.
- Updated the SWD monitor to resolve diagnostic addresses from the matching
  Keil MAP instead of using hardcoded SRAM addresses. This is necessary
  because added application state moves several counters.
- The UART RX/TX PHY, runtime timebase, stack and Jacdac heap sizes are
  unchanged. Historical protected-baseline hashes above describe the prior
  revision; they do not describe these intentional gyro/service changes.

Validation:

- Keil ARMCC 5.06 update 7 build 960, full rebuild:
  `0 Error(s), 81 Warning(s)` (same existing warnings).
- `Code=20000 RO-data=708 RW-data=348 ZI-data=2716`.
- Total ROM: `20724 / 24576` bytes (3852 bytes remaining); prior `23576`.
- RAM region: `0xBF8 / 0xC00`, still 8 bytes unallocated, including the
  original reserved 736-byte stack and 896-byte Jacdac heap. Linker stack
  analysis: 336 bytes plus unknown function-pointer/recursive paths.
- 29 tests execute the actual Keil AXF using Unicorn; HAL input/time and TX
  sinks are simulated. All pass. Coverage includes calibration windows,
  integer extrema, tick wrap, moving startup, slow rotation after calibration,
  atomic samples, absence/failure/recovery, config health checks, freshness,
  status changes, payload size and axis mapping. Results:
  `validation-tools/gyro-tests/latest-results.txt`.
- Existing Node host CRC/framing tests: 3/3 pass.
- Diagnostic MAP parsing checked against the generated symbols; no SWD
  connection or hardware flashing was performed.

Final artifacts:

- AXF SHA-256:
  `0A39D28636CB8BCB4A2D7BF311F8767D4F8D7D0B676A7D30F1B594320367D3C2`
- HEX SHA-256:
  `FDE241339AE2D359C15C6CFFF9CD38DCFBD4D7C442A1DFA8A485A632BC46596F`
- MAP SHA-256:
  `E70E7ADA74250085DE6127866BC137F57BCEFAD12ACF782EDF13867841447FEC`

The working post-build-repair source/project/HEX/MAP baseline is saved under
`validation-tools/gyro-before-20261021`. See `GYROSCOPE-CALIBRATION.md` for
thresholds, known limits and the required physical sample acceptance steps.
Real sensor noise, thermal behavior and communication under hardware load
remain to be measured on the production samples.

## 2026-10-21: fix the 0.3.0 I2C STOP/BUSY regression on hardware

The user connected ST-Link `004400564A00001156313848`. Attached without reset
and backed up all 24 KiB of Flash before changing the device. Flash matched
the local 0.3.0 HEX. The sensor at 0x6B returned part ID 0x05, but its
motion data remained invalid and configuration/recovery repeated with
HAL_BUSY. The main loop and Jacdac RX were still running; no stalled CPU
or growing heap was observed. Evidence: `validation-tools/gyro-live-fault-20261021`.

Puya HAL_I2C_Mem_Read returns before STOP has completed. The new one-shot
BUSY precheck therefore rejected a normal STOP tail, including the second
transaction in the accel/gyro pair. A new emulator model leaves SR2 BUSY
set for several reads after each HAL return, including NACK. Both new
STOP-tail tests failed on 0.3.0.

Version 0.3.1 permits 2048 bounded BUSY polls before a transaction, without
retries, delays or disabling interrupts. It still skips the HAL's 25ms BUSY
wait for a persistent fault. Restored the original 20ms transaction timeout
to allow 100kHz I2C transfers interrupted by Jacdac RX. Product identifier,
device description, service payload, calibration dwell and PHY are unchanged.

Validation:

- Full Keil build: 0 errors, same 81 warnings.
- Code=20028, RO-data=708, RW-data=348, ZI-data=2716; total ROM=20752 bytes.
- RAM remains 3064/3072 bytes with the original stack and custom heap sizes.
- 32 ARM firmware tests and 3 Node CRC/framing tests pass.
- Programmed through ST-Link, then read back and verified against local HEX.
- A 35-second running SWD capture found configuration valid in all 116
  snapshots, changing raw vectors and mapped service samples, 70 successive
  announce deadlines at 500ms spacing, 401 more RX frames and 300 more
  CRC-valid frames. Heap usage stayed at 620 bytes. These are running,
  non-atomic snapshots; transient sample-valid flags can overlap acquisition.
- User confirmed data is visible and the host connection is stable.
- Evidence: `validation-tools/gyro-live-repaired-20261021/capture.json`.
- Additional 12-second coherent capture contains 516 completed samples;
  this is intended for checking actual noise and calibration thresholds.
  The initial 2dps absolute gate rejects the observed ~7.5dps X-axis reading.

0.3.1 artifacts:

- AXF SHA-256: `B91C7754CB5250BB4A0143B60F7454C59F89996334DC67C6DC6DC040B650158A`.
- HEX SHA-256: `A6C43B0E9A70FB8162C13AB58C982D6E83B434A8FEC7276B5F94A67E8ABC62E2`.
- Source and build snapshots are in
  `validation-tools/gyro-031-stable-before-thresholds-20261021`.

## 2026-10-21: production-sample calibration gates and 600ms qualification (0.3.2)

The user confirmed the board was physically stationary while the host showed
rotation. A 12-second capture of 516 coherent samples measured ~7.5dps raw
X-axis bias and 0.12-0.16dps RMS gyro noise. The previous 2dps absolute guard
rejected this static bias, and the 0.25dps anchor-change threshold was too
tight for the measured noise. Adjusted these guards to 16dps and 1dps;
acceleration magnitude/stability checks are retained. A replay fixture is
derived from the first 160 coherent raw samples, with source SHA-256.

Per the user's updated request, calibration now has explicit phases:

1. WAITING: no candidate in progress.
2. QUALIFYING: require 600ms continuous stillness, accumulated from actual
   qualified sample gaps rather than a fixed assumed sample rate.
3. SAMPLING: collect 81 new qualified gyro samples for the bias mean.
4. COMPLETE: freeze accepted bias until reboot.

Motion, failed reads or gaps over 60ms during either unfinished phase
discard that attempt and require a new complete 600ms qualification. Old
sums are overwritten when sampling restarts. Qualification samples do not
enter the bias mean. Measured uncorrected data remains available before
completion. The new phase byte and uint16 elapsed counter use the prior
structure's padding; calibration state stays 32 bytes and RAM does not grow.

The 16dps initial-bias allowance cannot distinguish all constant rotation
around gravity from static offset. The device must actually stop during
initial calibration; no ongoing bias adaptation or low-rate deadband is used.

Validation:

- Full Keil build: 0 errors, same 81 warnings.
- Code=20108, RO-data=708, RW-data=348, ZI-data=2716.
- Total ROM=20832/24576 bytes; RAM=3064/3072 bytes, unchanged.
- Reserved stack=736 bytes, Jacdac heap=896 bytes, unchanged.
- 38 actual ARM firmware tests pass. They include qualification/sampling
  motion aborts, requalification, fresh-sum restart, elapsed-time gating,
  production trace replay, signed limit arithmetic and the I2C regression.
- Existing Node CRC/framing tests: 3/3 pass (run after the I2C change).
- Final 0.3.2 programmed via ST-Link, with live Flash matching local HEX/ELF.
- After an observed reset: first valid sample at 301ms, SAMPLING starts at
  911ms (610ms qualified stillness), COMPLETE at 2594ms (1683ms sampling).
  Accepted raw-axis biases: [7.5125, -1.3987, -0.4765] dps.
- Across the subsequent 393 coherent samples, corrected raw-axis means:
  [0.0281, -0.0016, 0.0100] dps, noise SD [0.1256, 0.1571, 0.1328] dps.
- Evidence: `validation-tools/gyro-live-032-20261021/startup-samples.json`.
  Its first record predates completion of the asynchronous reset; analysis
  uses the new epoch beginning at the second record (301ms).
- A further 30-second live capture kept configuration valid in all 100
  snapshots and phase COMPLETE throughout. RX increased by 314 frames,
  CRC-valid RX by 247; 60 successive 500ms announce deadlines were observed.
  Heap usage stayed 620 bytes. The final bias integers exactly matched the
  accepted startup bias, with no subsequent adaptation.
- Live evidence and verified full Flash backup:
  `validation-tools/gyro-live-032-20261021/capture.json` and `flash.bin`.
- The user subsequently confirmed the final 0.3.2 host acceptance check:
  readings respond to physical rotation, return near zero after stopping,
  and the connection remains stable.
- AXF SHA-256: `FAEB81D1FE10951ADBE2F4F3DC4ABCC2EED94C36BB7919165B7AB68E17438A07`.
- HEX SHA-256: `5ED80F7CBEB6F668A02EAAAB0DCCFC03C9FA838D488FFBC081A19668D7CEDEBB`.

## 2026-10-21: restore the host device name

At the user's request, changed both device-description literals from
`PY32 Jacdac Gyroscope Beacon` to `Jacdac Gyroscope Sensor v1.0`.
Control register DEVICE_DESCRIPTION (0x180) responds through
`app_get_dev_class_name()`, which now returns the requested text.

Full Keil rebuild passed with 0 errors and the existing 81 warnings; size
is unchanged. Compared the entire before/after HEX payload: it differs
only by replacement of the two equal-length name strings. Code, product
identifier 0x32516D56, firmware version 0.3.2 and calibration behavior are
identical to the previously verified build.

The first connection attempt could not read the target ID and did not
begin programming. After the user reconnected the device, programmed
through ST-Link and verified the full live Flash against the new HEX.
Read back both name literals as `Jacdac Gyroscope Sensor v1.0`.
An additional six-second capture showed valid configuration and completed
calibration throughout, with continuing Jacdac reception.

- Prior source/AXF/HEX/MAP: `validation-tools/gyro-032-before-name-20261021`.
- Live evidence: `validation-tools/gyro-live-032-name-20261021`.
- HEX SHA-256: `013CE9164BA1749B096C20F4E88E0D61045BAAA71C428932EA7C80D97A9B9160`.
- AXF SHA-256: `C4CE3DF23189AC2E5F85006C75AD7201C0FDDAE85F871D99B3B191A512C0EE9A`.

Product images remain associated with the host's product specification;
this change restores the firmware-supplied display text without guessing
a different product identifier.
