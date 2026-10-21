# Gyroscope firmware tests

These tests execute the application's actual Keil AXF in an ARM Thumb emulator.
The calibration, probe driver, application reading logic, Jacdac gyro service,
and common sensor helpers all run from the compiled image. Only HAL I/O, the
millisecond clock, and Jacdac TX/event sinks are substituted.

With Python 3.10+ and Keil ARM Compiler 5 installed, run from the project root:

```cmd
python -m venv .venv-gyro
.venv-gyro\Scripts\python -m pip install -r validation-tools\gyro-tests\requirements.txt
build_and_flash.cmd --build-only
.venv-gyro\Scripts\python validation-tools\gyro-tests\test_firmware.py
node --test validation-tools\jacdac-host-get\core.test.mjs
```

The runner prints the AXF SHA-256 to tie results to an exact build. Rebuild
first after changing C code; the tests run the AXF, not the source text.
The ELF symbol table supplies code and data addresses. Assertions check the
ARMCC layouts used by the isolated calibration and service fixtures. Fixture
memory beyond the MCU's SRAM is emulator-only; firmware RAM usage is checked
by the real Keil linker.

Tests cover moving startup, arbitrary resting orientation, 600ms stillness
qualification followed by 81 calibration samples, motion abort/requalification
in both phases, elapsed-time qualification at varying sample cadence,
fractional bias, movement/outlier/gap/failure resets, tick wrap, immutable
accepted bias, slow rotation after calibration, full-scale signed conversion,
complete-pair commit, failure throttling, late sensor detection, compatible
part selection, staged configuration, settling, readback mismatch, periodic
configuration checks, stuck BUSY, sample freshness, retained bias on recovery,
stream/GET payload compatibility, and validity/status transitions.

STOP-tail tests model Puya HAL returning before SR2 BUSY has cleared, including
the NACK path, consecutive reads/writes, and the extra config health read.
Both STOP-tail tests fail on the saved 0.3.0 image and pass on 0.3.1. To run
against that image, set `GYRO_TEST_AXF` to the saved AXF and
`GYRO_TEST_IO_TIMEOUT_MS=3`; the current image uses 20ms.

`stationary_sample_20261021.json` replays 160 coherent raw samples from the
physically stationary production board, including its ~7.5 degrees/s bias
and measured noise. It fails on 0.3.1's original calibration gates. Additional
tests check rejection of handheld tilt despite a large constant bias and
signed arithmetic at the updated 16 degrees/s initial-bias guard.

These are firmware behavior tests. They do not replace physical sensor,
electrical, UART timing, or interrupt-load validation. No test flashes or
connects to hardware.
