# 116 RX ASM Multi-Byte Anchor0545 Sample24 Progress Log

## Source

- Base project: `115-py32-qmi8656-jacdac-tim14-rx-asm-multibyte-anchor-edge0545-build`
- New project: `116-py32-qmi8656-jacdac-tim14-rx-asm-multibyte-anchor0545-sample24-build`

## Hypothesis

115 proved that the source-5 first-byte anchor can be moved to the 108 edge
evidence point: `first_start_delay = 0x0545`. The multi-byte ARMASM loop still
kept byte-to-byte cadence near `0x00f0`, but `crc_ok` stayed zero.

With `0x0545` treated as the start-low edge, the previous runtime sample offset
`0x11` is likely too early. This branch tests only one variable: move the
runtime in-byte sample point to the nominal bit center:

- `rx_bit_ticks = 0x18`
- target runtime sample offset: `1.5 * 0x18 = 0x24`
- implementation: `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`

## Intended Invariants

- Keep the 115 source-5 anchor: `APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x0545u`.
- Keep the 114/115 multi-byte ARMASM RX loop.
- Keep `APP_JACDAC_RX_USE_10BIT_CADENCE = 0u`.
- Keep byte-gap timeout frame closure and no early exit at expected length.
- Keep TX timing, status light, QMI8656 services, Jacdac-C control path, product id, name, icon, and service registration unchanged.

## Checks To Record

- Static check:
  - 116 has `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`.
  - 116 keeps `APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x0545u`.
  - 115 remains `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 19u`.
  - ARMASM file remains unchanged from 115.
- Build result.
- Flash result.
- SWD counters: `rx_complete`, `crc_ok`, `control_packet`, `product_id_get`.
- RX diagnostics: `last_received`, `expected_frame_bytes`, `data_left`, `end_code`, `rx_bit_ticks`, `sample_offset_ticks`, `first_start_delay`.
- First 16 bytes, start deltas, byte done ticks, done-to-next deltas, and direct `rxFrame` dump.

## Result Interpretation

- If `crc_ok > 0`: the sampling phase hypothesis is supported; continue toward control/product-id response.
- If cadence remains near `0x00f0` but `crc_ok = 0`: byte-to-byte hot path is still valid, but sample phase or anchor still needs refinement.
- If cadence degrades: sample point movement affected receive interpretation or next-start detection; compare start deltas and done-to-next against 115.

## 2026-07-08 Static Check

- 116 has `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 0u`.
- 116 keeps `APP_JACDAC_RX_SOURCE5_LEAD_TO_DATA_TICKS = 0x0545u`.
- 116 keeps `APP_JACDAC_RX_USE_10BIT_CADENCE = 0u`.
- 115 remains `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 19u`.
- `py32_jacdac_rx_fastpath.s` SHA256 matches 115:
  `BCA6C9D8816A08B78BB2058E7DF496134B3023D568206A6E67BD3887AA63F88D`.
- `Project.uvprojx` still includes `py32_jacdac_rx_fastpath.s` as `FileType = 2`.

## 2026-07-08 Build / Flash

- Keil build succeeded with `0 Error(s), 80 Warning(s)`.
- Program size: `Code=23592 RO-data=728 RW-data=300 ZI-data=2748`.
- pyOCD flash succeeded: erased `24576` bytes, programmed `24448` bytes.

## 2026-07-08 SWD Snapshot 1 After Reset + 8 s

- `rx_complete = 0x31`
- `crc_ok = 0x00`
- `control_packet = 0x00`
- `product_id_get = 0x00`
- `last_received = 0x14`
- `expected_frame_bytes = 0x2c`
- `data_left = 0xe8`
- `end_code = 0x04`
- `rx_bit_ticks = 0x18`
- `sample_offset_ticks = 0x24`
- `lead_low_ticks = 0x005c`
- `first_start_delay = 0x0545`
- `start_count = 0x10`
- `next_start_found_mask = 0x7fff`
- first 16 bytes: `76 83 20 10 08 00 a4 a9 d5 6a a0 13 08 09 46 f3`
- start deltas: `0000 00fc 0103 0101 0101 0101 0100 011f 013c 0105 0104 0102 0103 0101 0102 0103`
- done-to-next deltas: `001e 001e 001e 001e 001e 001e 003a 0056 001e 001e 001e 001e 001e 001e 001e ffff`
- direct `rxFrame` first 64 bytes:
  `76 83 20 10 08 00 a4 a9 d5 6a a0 13 08 09 46 f3 02 ff 00 ff 44 72 e2 93 99 fc 96 3c 00 ff 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

## 2026-07-08 SWD Snapshot 2 After Reset + 8 s

- `rx_complete = 0x4f`
- `crc_ok = 0x00`
- `control_packet = 0x00`
- `product_id_get = 0x00`
- `last_received = 0x1b`
- `expected_frame_bytes = 0x0e`
- `data_left = 0xe1`
- `end_code = 0x04`
- `rx_bit_ticks = 0x18`
- `sample_offset_ticks = 0x24`
- `lead_low_ticks = 0x00c8`
- `first_start_delay = 0x0545`
- `start_count = 0x10`
- `next_start_found_mask = 0x7fff`
- first 16 bytes: `e8 04 02 3b 4d f0 77 26 28 ba 31 00 00 80 74 02`
- start deltas: `0000 0104 0101 0101 00fc 013c 0104 010b 011f 011e 0105 0103 0100 011c 0163 0104`
- done-to-next deltas: `001e 001e 001e 001e 0056 001e 002c 003a 003a 001e 001e 001e 003a 0080 001e ffff`
- direct `rxFrame` first 64 bytes:
  `e8 04 02 3b 4d f0 77 26 28 ba 31 00 00 80 74 02 03 ad c4 45 47 28 ac b7 fe 00 ff 8f 00 ff 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

## 2026-07-08 Conclusion

The sample24 change landed as intended: runtime `sample_offset_ticks = 0x24`.
It did not produce CRC-valid frames. More importantly, start deltas regressed
from the 115 mostly-`0x00ef..0x00f2` range to mostly `0x0100..0x0105`, with
occasional larger misses. This supports a new root-cause hypothesis: when the
bit7 sample is moved toward the nominal center, the helper reaches
`rx_wait_next_start` late enough that the recorded next byte `start_tick` is the
detection time, not the true start-low edge.

Next branch should keep the 116 sample phase and test only next-start detection
latency compensation, for example subtracting about `0x0c` ticks from the
recorded next-start tick inside the ARMASM loop.
