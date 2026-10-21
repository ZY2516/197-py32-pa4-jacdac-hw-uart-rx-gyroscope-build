; Cortex-M0/M0+ Jacdac RX multi-byte sampler.
; r0 = APP_JacdacRxMultiAsmState *

                PRESERVE8
                THUMB

                AREA    |.text|, CODE, READONLY
                EXPORT  APP_JacdacRxMultiByteAsm

APP_JacdacRxMultiByteAsm
                CMP     r0, #0
                BNE     rx_arg_ok
                MOVS    r0, #0
                SUBS    r0, r0, #1
                BX      lr

rx_arg_ok
                PUSH    {r4-r7, lr}
                MOV     r4, r0          ; state

                LDR     r0, [r4, #12]   ; buffer
                CMP     r0, #0
                BNE     rx_buffer_ok
                MOVS    r0, #0
                SUBS    r0, r0, #1
                POP     {r4-r7, pc}

rx_buffer_ok
                LDR     r7, [r4, #8]    ; pin_mask
                MOVS    r5, #0          ; received/current index
                LDRH    r6, [r4, #28]   ; current start_tick

rx_byte_loop
                LDR     r0, [r4, #24]   ; maxbytes
                CMP     r5, r0
                BCC     rx_have_room
                MOVS    r0, #5
                MOV     r1, r4
                ADDS    r1, r1, #37
                STRB    r0, [r1, #0]
                STRH    r5, [r4, #38]
                MOVS    r0, #0
                POP     {r4-r7, pc}

rx_have_room
                LDRH    r2, [r4, #32]   ; sample_offset_ticks
                ADDS    r2, r2, r6      ; first sample deadline
                MOVS    r3, #0          ; assembled byte

rx_wait_bit0
                LDR     r1, [r4, #4]
                LDRH    r0, [r1, #0]
                SUBS    r0, r0, r2
                LSLS    r0, r0, #16
                BMI     rx_wait_bit0
                LDR     r1, [r4, #0]
                LDR     r0, [r1, #0]
                TST     r0, r7
                BEQ     rx_bit0_done
                MOVS    r0, #1
                ORRS    r3, r0
rx_bit0_done
                LDRH    r0, [r4, #30]
                ADDS    r2, r2, r0

rx_wait_bit1
                LDR     r1, [r4, #4]
                LDRH    r0, [r1, #0]
                SUBS    r0, r0, r2
                LSLS    r0, r0, #16
                BMI     rx_wait_bit1
                LDR     r1, [r4, #0]
                LDR     r0, [r1, #0]
                TST     r0, r7
                BEQ     rx_bit1_done
                MOVS    r0, #2
                ORRS    r3, r0
rx_bit1_done
                LDRH    r0, [r4, #30]
                ADDS    r2, r2, r0

rx_wait_bit2
                LDR     r1, [r4, #4]
                LDRH    r0, [r1, #0]
                SUBS    r0, r0, r2
                LSLS    r0, r0, #16
                BMI     rx_wait_bit2
                LDR     r1, [r4, #0]
                LDR     r0, [r1, #0]
                TST     r0, r7
                BEQ     rx_bit2_done
                MOVS    r0, #4
                ORRS    r3, r0
rx_bit2_done
                LDRH    r0, [r4, #30]
                ADDS    r2, r2, r0

rx_wait_bit3
                LDR     r1, [r4, #4]
                LDRH    r0, [r1, #0]
                SUBS    r0, r0, r2
                LSLS    r0, r0, #16
                BMI     rx_wait_bit3
                LDR     r1, [r4, #0]
                LDR     r0, [r1, #0]
                TST     r0, r7
                BEQ     rx_bit3_done
                MOVS    r0, #8
                ORRS    r3, r0
rx_bit3_done
                LDRH    r0, [r4, #30]
                ADDS    r2, r2, r0

rx_wait_bit4
                LDR     r1, [r4, #4]
                LDRH    r0, [r1, #0]
                SUBS    r0, r0, r2
                LSLS    r0, r0, #16
                BMI     rx_wait_bit4
                LDR     r1, [r4, #0]
                LDR     r0, [r1, #0]
                TST     r0, r7
                BEQ     rx_bit4_done
                MOVS    r0, #16
                ORRS    r3, r0
rx_bit4_done
                LDRH    r0, [r4, #30]
                ADDS    r2, r2, r0

rx_wait_bit5
                LDR     r1, [r4, #4]
                LDRH    r0, [r1, #0]
                SUBS    r0, r0, r2
                LSLS    r0, r0, #16
                BMI     rx_wait_bit5
                LDR     r1, [r4, #0]
                LDR     r0, [r1, #0]
                TST     r0, r7
                BEQ     rx_bit5_done
                MOVS    r0, #32
                ORRS    r3, r0
rx_bit5_done
                LDRH    r0, [r4, #30]
                ADDS    r2, r2, r0

rx_wait_bit6
                LDR     r1, [r4, #4]
                LDRH    r0, [r1, #0]
                SUBS    r0, r0, r2
                LSLS    r0, r0, #16
                BMI     rx_wait_bit6
                LDR     r1, [r4, #0]
                LDR     r0, [r1, #0]
                TST     r0, r7
                BEQ     rx_bit6_done
                MOVS    r0, #64
                ORRS    r3, r0
rx_bit6_done
                LDRH    r0, [r4, #30]
                ADDS    r2, r2, r0

rx_wait_bit7
                LDR     r1, [r4, #4]
                LDRH    r0, [r1, #0]
                SUBS    r0, r0, r2
                LSLS    r0, r0, #16
                BMI     rx_wait_bit7
                LDR     r1, [r4, #0]
                LDR     r0, [r1, #0]
                TST     r0, r7
                BEQ     rx_bit7_done
                MOVS    r0, #128
                ORRS    r3, r0

rx_bit7_done
                LDR     r1, [r4, #12]
                STRB    r3, [r1, r5]

                LDR     r1, [r4, #4]
                LDRH    r2, [r1, #0]    ; byte_done_tick

                MOV     r1, r4
                ADDS    r1, r1, #36
                LDRB    r0, [r1, #0]
                CMP     r5, r0
                BCS     rx_skip_diag_store
                MOV     r0, r5
                LSLS    r0, r0, #1
                LDR     r1, [r4, #16]
                STRH    r6, [r1, r0]
                LDR     r1, [r4, #20]
                STRH    r2, [r1, r0]

rx_skip_diag_store
                LDRH    r3, [r4, #34]
                ADDS    r3, r3, r2      ; next-start timeout deadline

rx_wait_next_start
                LDR     r1, [r4, #0]
                LDR     r0, [r1, #0]
                TST     r0, r7
                BEQ     rx_next_start_found

                LDR     r1, [r4, #4]
                LDRH    r0, [r1, #0]
                SUBS    r0, r0, r3
                LSLS    r0, r0, #16
                BMI     rx_wait_next_start

                ADDS    r5, r5, #1
                STRH    r5, [r4, #38]
                MOVS    r0, #4
                MOV     r1, r4
                ADDS    r1, r1, #37
                STRB    r0, [r1, #0]
                MOVS    r0, #0
                POP     {r4-r7, pc}

rx_next_start_found
                LDR     r1, [r4, #4]
                LDRH    r6, [r1, #0]    ; next byte start_tick
                LDRH    r0, [r4, #40]   ; compensate low-detect latency
                SUBS    r6, r6, r0
                ADDS    r5, r5, #1
                B       rx_byte_loop

                END
