# 118 RX ASM Multi-Byte Sample24 Next-Rewind10 Progress Log

## Source

- Base project: `117-py32-qmi8656-jacdac-tim14-rx-asm-multibyte-sample24-nextrewind0c-build`
- New project: `118-py32-qmi8656-jacdac-tim14-rx-asm-multibyte-sample24-nextrewind10-build`

## Hypothesis

117 showed that next-start rewind compensation is the right timing variable:
with sample offset held at `0x24`, start deltas moved from 116's mostly
`0x0100..0x0105` back toward the theoretical `0x00f0`.

However, 117 still had no CRC-valid frames and residual deltas often landed at
`0x00f3..0x00f8`. This branch keeps the same sample phase and anchor, and tests
only a larger next-start rewind:

- old rewind: `0x000c`
- new rewind: `0x0010`

## Intended Invariants

- Keep `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`, runtime sample offset `0x24`.
- Keep `APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x0545u`.
- Keep `APP_JACDAC_RX_USE_10BIT_CADENCE = 0u`.
- Keep the same ARMASM implementation shape as 117.
- Keep byte-gap timeout frame closure and no early exit at expected length.
- Keep TX timing, status light, QMI8656 services, Jacdac-C control path, product id, name, icon, and service registration unchanged.

## Checks To Record

- Static check:
  - 118 has `APP_JACDAC_RX_NEXT_START_REWIND_TICKS = 0x0010u`.
  - 118 still has `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`.
  - 117 remains `APP_JACDAC_RX_NEXT_START_REWIND_TICKS = 0x000cu`.
- Build result.
- Flash result.
- SWD counters: `rx_complete`, `crc_ok`, `control_packet`, `product_id_get`.
- RX diagnostics: `last_received`, `expected_frame_bytes`, `data_left`, `end_code`, `rx_bit_ticks`, `sample_offset_ticks`, `first_start_delay`.
- First 16 bytes, start deltas, byte done ticks, done-to-next deltas, and direct `rxFrame` dump.

## Result Interpretation

- If start deltas tighten closer to `0x00f0` and bytes improve, larger rewind is supported.
- If start deltas become early/unstable or `last_received` drops, rewind `0x10` is too much.
- If deltas are good but `crc_ok = 0`, next blocker is likely first-byte anchor/sample semantics or bit value inversion/order, not inter-byte C overhead.

## 2026-07-08 Static Check

- 118 has `APP_JACDAC_RX_NEXT_START_REWIND_TICKS = 0x0010u`.
- 118 keeps `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`.
- 118 keeps `APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x0545u`.
- 118 keeps `APP_JACDAC_RX_USE_10BIT_CADENCE = 0u`.
- 117 remains `APP_JACDAC_RX_NEXT_START_REWIND_TICKS = 0x000cu`.
- C diff from 117 is only the rewind constant.
- ARMASM SHA256 matches 117:
  `A5B52271D64E2A5937240FD457ADCCBC6E296083DFCEA9B34B788D459D578A3D`.

## 2026-07-08 Build / Flash

- Keil build succeeded with `0 Error(s), 80 Warning(s)`.
- Program size: `Code=23600 RO-data=728 RW-data=300 ZI-data=2748`.
- pyOCD flash succeeded: erased `4096` bytes, programmed `4096` bytes, skipped `20352` bytes.

## 2026-07-08 SWD Snapshot 1 After Reset + 8 s

- `rx_complete = 0x51`
- `crc_ok = 0x00`
- `control_packet = 0x00`
- `product_id_get = 0x00`
- `last_received = 0x16`
- `expected_frame_bytes = 0x68`
- `data_left = 0xe6`
- `end_code = 0x04`
- `rx_bit_ticks = 0x18`
- `sample_offset_ticks = 0x24`
- `lead_low_ticks = 0x00c8`
- `first_start_delay = 0x0545`
- `start_count = 0x10`
- `next_start_found_mask = 0x7fff`
- first 16 bytes: `6c 87 5c 04 04 04 04 70 53 53 3b 23 e6 02 83 00`
- start deltas: `0000 0109 00f5 00ec 00f2 00f2 00f2 00f2 0102 00ec 00fa 00ed 00f4 00ed 00f2 00f4`
- done-to-next deltas: `002a 000e 000e 000e 000e 000e 000e 001c 000e 001c 000e 000e 000e 000e 000e ffff`
- direct `rxFrame` first 64 bytes:
  `6c 87 5c 04 04 04 04 70 53 53 3b 23 e6 02 83 00 11 fb 02 fe 00 fe 52 fb c4 27 64 e5 a5 79 ff 00 fe 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

## 2026-07-08 SWD Snapshot 2 After Reset + 8 s

- `rx_complete = 0x4c`
- `crc_ok = 0x00`
- `control_packet = 0x00`
- `product_id_get = 0x00`
- `last_received = 0x16`
- `expected_frame_bytes = 0x36`
- `data_left = 0xe6`
- `end_code = 0x04`
- `rx_bit_ticks = 0x18`
- `sample_offset_ticks = 0x24`
- `lead_low_ticks = 0x005c`
- `first_start_delay = 0x0545`
- `start_count = 0x10`
- `next_start_found_mask = 0x7fff`
- first 16 bytes: `db 43 2a 02 02 02 02 52 1b 53 39 23 e4 01 81 01`
- start deltas: `0000 00ef 00f4 00f4 00f2 00f2 00f2 00f2 00f4 00ec 00fa 00ec 00f4 00f5 00f2 00f3`
- done-to-next deltas: `000e 000e 000e 000e 000e 000e 000e 000e 000e 001c 000e 000e 000e 000e 000e ffff`
- direct `rxFrame` first 64 bytes:
  `db 43 2a 02 02 02 02 52 1b 53 39 23 e4 01 81 01 11 79 02 fe 00 fe 1e 00 fe b7 fc 00 fe 79 ff 00 fe 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

## 2026-07-08 Conclusion

The `0x10` rewind did not produce CRC-valid frames. It moved some deltas closer
to `0x00f0`, but also produced early values around `0x00ec` and occasional late
values above `0x0100`. Compared with 117, it is not clearly better.

Current evidence after 116/117/118:

- PY32 can run the deterministic ARMASM multi-byte RX loop.
- Next-start detection latency is real and compensating it can move cadence.
- CRC still fails even when sample offset is `0x24` and byte-to-byte deltas are
  near the theoretical region.

The next useful experiment should stop treating a single submitted sample phase
as enough evidence. Add a SWD-only multi-phase diagnostic that captures the
same first bytes at a few offsets around the current anchor without changing
what is submitted to `jd_rx_completed()`. That can reveal whether any phase
produces plausible Jacdac headers before making more firmware-control changes.
