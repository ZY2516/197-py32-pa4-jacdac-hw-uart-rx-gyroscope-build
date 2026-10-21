# 115 RX ASM Multi-Byte Anchor Edge0545 Progress Log

## Source

- Base project: `114-py32-qmi8656-jacdac-tim14-rx-asm-multibyte-fastpath-build`
- New project: `115-py32-qmi8656-jacdac-tim14-rx-asm-multibyte-anchor-edge0545-build`

## Hypothesis

114 proves that the byte-to-byte receive loop can stay close to the theoretical
`0x00f0` cadence. Since `crc_ok` is still zero, the next likely variable is
first-byte alignment.

108 edge evidence saw the first stable post-lead transition at about `0x0545`
ticks after lead rise. 114 still uses source-5 anchor `0x0558`, so 115 tests
only this anchor shift:

- old source-5 anchor: `lead_rise + 0x0558`
- new source-5 anchor: `lead_rise + 0x0545`

## Intended Invariants

- Keep the 114 multi-byte ARMASM RX loop.
- Keep `APP_JACDAC_RX_SAMPLE_ADVANCE_TICKS = 19u`, runtime sample offset `0x0011`.
- Keep `APP_JACDAC_RX_USE_10BIT_CADENCE = 0u`.
- Keep TX timing, status light, QMI8656 services, Jacdac-C control path, product id, name, icon, and service registration unchanged.

## Checks To Record

- Build result.
- Flash result.
- SWD counters: `rx_complete`, `crc_ok`, `control_packet`, `product_id_get`.
- RX diagnostics: `last_received`, `expected_frame_bytes`, `data_left`, `end_code`, `sample_offset_ticks`, `first_start_delay`.
- First 16 bytes, start deltas, byte done ticks, done-to-next deltas, and direct `rxFrame` dump.

## 2026-07-08 Build / Flash

- Keil build succeeded with `0 Error(s), 80 Warning(s)`.
- Program size: `Code=23596 RO-data=728 RW-data=300 ZI-data=2748`.
- pyOCD flash succeeded on ST-Link `006000133900001456314D43`.

## 2026-07-08 SWD Snapshot 1 After 8 s

- `rx_complete = 0x6d`
- `crc_ok = 0x00`
- `control_packet = 0x00`
- `product_id_get = 0x00`
- `last_received = 0x1d`
- `expected_frame_bytes = 0x8c`
- `data_left = 0xdf`
- `end_code = 0x04`
- `rx_bit_ticks = 0x18`
- `sample_offset_ticks = 0x11`
- `lead_low_ticks = 0x005c`
- `first_start_delay = 0x0545`
- `start_count = 0x10`
- `next_start_found_mask = 0x7fff`
- first 16 bytes: `97 88 80 c7 c9 f0 ee 98 8b 8c db 86 80 80 80 0f`
- start deltas: `0000 00eb 00f0 00ef 00ea 00f2 00f2 00eb 00f1 00f2 00f1 00eb 00f1 00ef 00ef 00ef`
- done-to-next deltas: `001e 001e 001e 001e 001e 001e 001e 001e 001e 001e 001e 001e 001e 001e 001e ffff`
- direct `rxFrame` first 64 bytes:
  `97 88 80 c7 c9 f0 ee 98 8b 8c db 86 80 80 80 0f 87 00 00 eb 89 99 1e 07 99 e5 1b 00 fc 4b 1e 00 fe 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

## 2026-07-08 SWD Snapshot 2 After Reset + 8 s

- `rx_complete = 0x67`
- `crc_ok = 0x00`
- `control_packet = 0x00`
- `product_id_get = 0x00`
- `last_received = 0x12`
- `expected_frame_bytes = 0x24`
- `data_left = 0xea`
- `end_code = 0x04`
- `sample_offset_ticks = 0x11`
- `lead_low_ticks = 0x00c8`
- `first_start_delay = 0x0545`
- `start_count = 0x10`
- `next_start_found_mask = 0x7fff`
- first 16 bytes: `d5 1e 18 18 09 4a 9a 22 d1 41 05 01 05 11 41 ff`
- start deltas: `0000 00eb 00f7 00f0 00fe 0128 00f1 00f2 00fe 00f2 00f0 00f0 00ef 00f0 00f0 00f0`
- done-to-next deltas: `001e 002c 001e 002c 0056 001e 001e 002c 001e 001e 001e 001e 001e 001e 001e ffff`
- direct `rxFrame` first 64 bytes:
  `d5 1e 18 18 09 4a 9a 22 d1 41 05 01 05 11 41 ff 00 fc 00 eb 89 15 1e 87 91 e5 1b 00 fe 4b 1e 00 fc 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

## 2026-07-08 Conclusion

The anchor shift landed as intended: `first_start_delay = 0x0545`.
It did not produce CRC-valid frames. Start deltas remained mostly near
`0x00f0`, so the multi-byte asm cadence still holds.

New interpretation: with the asm loop now detecting real next-start lows, the
runtime `sample_offset_ticks = 0x11` is likely too early for in-byte sampling.
If `0x0545` is the true start-low edge, the ideal bit0 center is about
`0x0545 + 1.5 * 0x18 = 0x0569`, meaning a sample offset around `0x24`, not
`0x11`. The next branch should keep the 115 anchor and test only sample offset
`0x24`.
