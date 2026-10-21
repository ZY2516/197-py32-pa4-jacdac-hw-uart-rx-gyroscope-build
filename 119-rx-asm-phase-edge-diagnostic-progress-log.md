# 119 RX ASM Phase Edge Diagnostic Progress Log

## Source

- Base project: `118-py32-qmi8656-jacdac-tim14-rx-asm-multibyte-sample24-nextrewind10-build`
- New project: `119-py32-qmi8656-jacdac-tim14-rx-asm-phase-edge-diagnostic-build`

## Hypothesis

116/117/118 showed that the Cortex-M0 ARMASM multi-byte loop can keep
byte-to-byte timing near the theoretical region, but `crc_ok` remains zero.
The next missing evidence is whether any sampling phase around the current
anchor produces plausible Jacdac header bytes.

119 consumes the first RX frame after reset as a SWD-only diagnostic frame:

- capture an edge timeline from `lead_rise_tick` across the first 8 byte times;
- reconstruct first 8 bytes for five candidate sample offsets:
  - `0x18`
  - `0x1c`
  - `0x20`
  - `0x24`
  - `0x28`
- store results in `g_app_jd_rx_phase_diag_*` symbols.

After that first diagnostic frame, the 118 main RX path remains available for
normal frames.

## Intended Invariants

- Keep `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`, runtime sample offset `0x24`.
- Keep `APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x0545u`.
- Keep `APP_JACDAC_RX_NEXT_START_REWIND_TICKS = 0x0010u`.
- Keep `APP_JACDAC_RX_USE_10BIT_CADENCE = 0u`.
- Keep the ARMASM runtime byte loop unchanged from 118.
- Keep TX timing, status light, QMI8656 services, Jacdac-C control path, product id, name, icon, and service registration unchanged.

## Checks To Record

- Static check:
  - 119 has `g_app_jd_rx_phase_diag_*` SWD symbols.
  - 119 ARMASM file matches 118.
  - 118 remains without phase diagnostic capture.
- Build result.
- Flash result.
- SWD counters:
  - `rx_complete`
  - `crc_ok`
  - `control_packet`
  - `product_id_get`
- Phase diagnostic:
  - `phase_diag_done`
  - `phase_diag_status`
  - `base_tick`
  - `start_delay`
  - `bit_ticks`
  - `edge_count`
  - `offsets`
  - `phase bytes`
  - `edge ticks`
- Runtime timing after diagnostic:
  - `last_received`
  - `expected_frame_bytes`
  - `end_code`
  - start deltas
  - direct `rxFrame` dump

## Result Interpretation

- If one phase yields plausible Jacdac header/size bytes and neighboring phases do not, use that phase for the next main RX branch.
- If no phase is plausible but edge ticks look clean, revisit first-byte anchor or byte-start reconstruction.
- If edge ticks are sparse or status reports entry was already low, the diagnostic started too late and needs an earlier hook.

## 2026-07-08 Static Check

- 119 keeps `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`.
- 119 keeps `APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x0545u`.
- 119 keeps `APP_JACDAC_RX_NEXT_START_REWIND_TICKS = 0x0010u`.
- 119 adds `g_app_jd_rx_phase_diag_*` SWD symbols.
- 118 remains without phase diagnostic capture.
- ARMASM SHA256 matches 118:
  `A5B52271D64E2A5937240FD457ADCCBC6E296083DFCEA9B34B788D459D578A3D`.
