# 123 Phase Window Diagnostic Progress Log

## Hypothesis

119 saturated its edge array before the predicted first start. Start capture at
`start_tick`, expand the edge array from 8 to 24 entries, and leave the 119
ARMASM RX path unchanged.

## Verification

- Test-first static verifier failed before the change and passed afterward.
- Keil rebuild: `0 Error(s), 80 Warning(s)`.
- pyOCD flash through ST-Link `006000133900001456314D43`: success.
- SWD after 15 seconds:
  - `rx_complete = 0x130`
  - `crc_ok = 0`
  - `control_packet = 0`
  - `product_id_get = 0`
  - `first_start_delay = 0x0545`
  - `edge_count = 24`
  - edges: `0558 05a0 05c8 05f0 0618 0640 0668 0690 06b8 06fa 0722 077e 07a6 0884 08ac 08ee 0930 0972 099a 09f6 0a1e 0a60 0aa2 0aca`

## Conclusion

The corrected window captures after the predicted start, but the minimum
recorded edge interval is about `0x28 = 40` ticks while the wire bit time is
about 24 ticks. The C polling loop misses bit-level transitions and cannot be
used for phase reconstruction. Do not continue expanding this C edge recorder.
