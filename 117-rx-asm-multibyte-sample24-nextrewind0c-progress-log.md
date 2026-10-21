# 117 RX ASM Multi-Byte Sample24 Next-Rewind0c Progress Log

## Source

- Base project: `116-py32-qmi8656-jacdac-tim14-rx-asm-multibyte-anchor0545-sample24-build`
- New project: `117-py32-qmi8656-jacdac-tim14-rx-asm-multibyte-sample24-nextrewind0c-build`

## Hypothesis

116 proved that runtime `sample_offset_ticks = 0x24` lands, but it moved
byte-to-byte `start_deltas` later than theory: mostly `0x0100..0x0105` instead
of near `0x00f0`. The likely issue is that the ARMASM loop records the time
when it observes low, not the true low edge.

This branch keeps sample24 and first anchor 0x0545, and tests only one new
variable: subtract `0x000c` ticks from the recorded next-start tick after the
low is detected.

## Intended Invariants

- Keep `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`, runtime sample offset `0x24`.
- Keep `APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x0545u`.
- Keep `APP_JACDAC_RX_USE_10BIT_CADENCE = 0u`.
- Keep byte-gap timeout frame closure and no early exit at expected length.
- Keep TX timing, status light, QMI8656 services, Jacdac-C control path, product id, name, icon, and service registration unchanged.

## Checks To Record

- Static check:
  - 117 has `APP_JACDAC_RX_NEXT_START_REWIND_TICKS = 0x000cu`.
  - 117 still has `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`.
  - 116 remains without next-start rewind compensation.
- Build result.
- Flash result.
- SWD counters: `rx_complete`, `crc_ok`, `control_packet`, `product_id_get`.
- RX diagnostics: `last_received`, `expected_frame_bytes`, `data_left`, `end_code`, `rx_bit_ticks`, `sample_offset_ticks`, `first_start_delay`.
- First 16 bytes, start deltas, byte done ticks, done-to-next deltas, and direct `rxFrame` dump.

## Result Interpretation

- If start deltas move from `0x0100..0x0105` back near `0x00f0` while sample offset stays `0x24`, the next-start latency hypothesis is supported.
- If `crc_ok > 0`, continue toward control/product-id.
- If deltas improve but `crc_ok = 0`, next variable is likely first-byte anchor or a smaller/larger rewind, not C inter-byte overhead.

## 2026-07-08 Static Check

- 117 has `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`.
- 117 keeps `APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x0545u`.
- 117 has `APP_JACDAC_RX_NEXT_START_REWIND_TICKS = 0x000cu`.
- 117 keeps `APP_JACDAC_RX_USE_10BIT_CADENCE = 0u`.
- 116 remains without `APP_JACDAC_RX_NEXT_START_REWIND_TICKS`.
- C diff from 116 is limited to the rewind constant, appended asm-state field, and state assignment.
- ARMASM diff from 116 is limited to:
  - `LDRH r0, [r4, #40]`
  - `SUBS r6, r6, r0`

## 2026-07-08 Build / Flash

- Keil build succeeded with `0 Error(s), 80 Warning(s)`.
- Program size: `Code=23600 RO-data=728 RW-data=300 ZI-data=2748`.
- pyOCD flash succeeded: erased `24576` bytes, programmed `24448` bytes.

## 2026-07-08 SWD Snapshot 1 After Reset + 8 s

- `rx_complete = 0x56`
- `crc_ok = 0x00`
- `control_packet = 0x00`
- `product_id_get = 0x00`
- `last_received = 0x0f`
- `expected_frame_bytes = 0x5a`
- `data_left = 0xed`
- `end_code = 0x04`
- `rx_bit_ticks = 0x18`
- `sample_offset_ticks = 0x24`
- `lead_low_ticks = 0x0077`
- `first_start_delay = 0x0545`
- `start_count = 0x0f`
- `next_start_found_mask = 0x3fff`
- first 16 bytes: `80 d5 4e 6a 6a 41 4f 70 24 30 30 06 fe 00 ff 00`
- start deltas: `0000 00f8 00f6 00f5 00f5 00f5 00f3 00f6 00f4 00f3 00f3 0147 00f3 015a 00f1 0000`
- done-to-next deltas: `0012 0012 0012 0012 0012 0012 0012 0012 0012 0012 0066 0012 0074 0012 ffff 0000`
- direct `rxFrame` first 64 bytes:
  `80 d5 4e 6a 6a 41 4f 70 24 30 30 06 fe 00 ff 00 e6 37 64 e5 96 38 ff 00 fe fe 32 fc 96 78 ff 00 fe 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

## 2026-07-08 SWD Snapshot 2 After Reset + 8 s

- `rx_complete = 0x5f`
- `crc_ok = 0x00`
- `control_packet = 0x00`
- `product_id_get = 0x00`
- `last_received = 0x0b`
- `expected_frame_bytes = 0xfc`
- `data_left = 0xf1`
- `end_code = 0x04`
- `rx_bit_ticks = 0x18`
- `sample_offset_ticks = 0x24`
- `lead_low_ticks = 0x0119`
- `first_start_delay = 0x0545`
- `start_count = 0x0b`
- `next_start_found_mask = 0x03ff`
- first 16 bytes: `e6 11 f5 01 81 01 c2 8f 80 00 ff 00 00 00 00 00`
- start deltas: `0000 010e 00f3 00f7 00f2 00f3 012a 00f4 00f6 01e0 00f1 0000 0000 0000 0000 0000`
- done-to-next deltas: `002e 0012 0012 0012 0012 004a 0012 0012 0100 0012 ffff 0000 0000 0000 0000 0000`
- direct `rxFrame` first 64 bytes:
  `e6 11 f5 01 81 01 c2 8f 80 00 ff 00 00 00 00 00 08 08 a8 cf c5 82 52 fb e2 93 76 fc b6 78 ff 00 fe 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

## 2026-07-08 Conclusion

The `0x0c` next-start rewind hypothesis is supported at the hot-path timing
level: with runtime sample offset still at `0x24`, start deltas moved from
116's mostly `0x0100..0x0105` range back toward `0x00f0`.

It is not yet sufficient for Jacdac frames: `crc_ok`, `control_packet`, and
`product_id_get` remain zero. The residual deltas are still commonly
`0x00f3..0x00f8` with occasional late misses. Next branch should keep anchor
`0x0545` and sample offset `0x24`, and test only a larger next-start rewind
such as `0x10`.
