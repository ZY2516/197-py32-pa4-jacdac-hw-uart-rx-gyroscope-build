"""Execute the built ARM firmware; substitute only HAL I/O and Jacdac TX.

Usage (after a Keil rebuild): python validation-tools/gyro-tests/test_firmware.py
The ELF symbol table supplies all firmware addresses; no C algorithm is copied.
"""
import io
import hashlib
import json
import math
import os
import struct
import sys
import unittest
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_MODE_MCLASS, UC_MODE_THUMB
from unicorn.arm_const import (
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1,
    UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP,
)

ROOT = Path(__file__).resolve().parents[2]
AXF = Path(os.environ.get("GYRO_TEST_AXF", str(ROOT / "MDK-ARM/Output/py32f002b_qmi8656_jacdac_clean_hybrid_phy_build.axf")))
IO_TIMEOUT_MS = int(os.environ.get("GYRO_TEST_IO_TIMEOUT_MS", "20"))
READING = 0x1101
STATUS = 0x1103
CAL = 0x20002000
VECTORS = 0x20002100
SERVICE = 0x20002200
PACKET = 0x20002300
HANDLE = 0x20002400
STOP = 0x0800FFF0


class Firmware:
    def __init__(self):
        elf = ELFFile(io.BytesIO(AXF.read_bytes()))
        self.symbols = {s.name: (s["st_value"], s["st_size"])
                        for s in elf.get_section_by_name(".symtab").iter_symbols() if s.name}
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(0x08000000, 0x10000)
        # Extra emulated SRAM holds isolated test fixtures, not firmware allocations.
        self.uc.mem_map(0x20000000, 0x10000)
        self.uc.mem_map(0x40005000, 0x1000)
        for section in elf.iter_sections():
            if section["sh_flags"] & 2 and section["sh_type"] != "SHT_NOBITS" and section["sh_size"]:
                self.uc.mem_write(section["sh_addr"], section.data())
        self.ms = 0
        self.devices = {0x6B: {0: 5}}
        self.accel = (0, 0, 4096)
        self.gyro = (12, -8, 4)
        self.io = []
        self.sent = []
        self.events = []
        self.fail_reg = None
        self.io_delay_ms = 0
        self.stop_tail_reads = 0
        self.busy_reads_left = None
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read_sr2, begin=0x40005418, end=0x40005418)
        self.hook("HAL_GetTick", lambda: self.ms)
        self.hook("HAL_I2C_Mem_Read", lambda: self.i2c(False))
        self.hook("HAL_I2C_Mem_Write", lambda: self.i2c(True))
        self.hook("HAL_Delay", self.unexpected_delay)
        self.hook("jd_send", self.send)
        self.hook("jd_send_event_ext", self.event)
        self.uc.mem_write(HANDLE, struct.pack("<I", 0x40005400))
        self.call("APP_QmiProbeBindBus", HANDLE)
        # ARMCC's concrete layouts are checked against the generated ELF.
        assert self.symbols["g_gyro_calibration"][1] == 32
        assert self.symbols["g_last_probe_result"][1] == 30

    def addr(self, name):
        return self.symbols[name][0]

    def hook(self, name, fn):
        address = self.addr(name) & ~1

        def dispatch(uc, _address, _size, _data):
            value = fn()
            uc.reg_write(UC_ARM_REG_R0, 0 if value is None else value & 0xFFFFFFFF)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

        self.uc.hook_add(UC_HOOK_CODE, dispatch, begin=address, end=address)

    def args(self):
        return [self.uc.reg_read(r) for r in
                (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]

    def read(self, address, size):
        return bytes(self.uc.mem_read(address, size))

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def set_ms(self, ms):
        self.ms = ms & 0xFFFFFFFF
        self.uc.mem_write(self.addr("now"), struct.pack("<I", (self.ms * 1000) & 0xFFFFFFFF))

    def call(self, name, *args):
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            self.uc.reg_write(reg, value & 0xFFFFFFFF)
        self.uc.reg_write(UC_ARM_REG_SP, 0x2000F000)
        self.uc.reg_write(UC_ARM_REG_LR, STOP | 1)
        self.uc.emu_start(self.addr(name) | 1, STOP, timeout=1000000, count=200000)
        if self.uc.reg_read(UC_ARM_REG_PC) != STOP:
            raise AssertionError(f"{name} did not return (possible blocking loop)")
        return self.uc.reg_read(UC_ARM_REG_R0)

    def unexpected_delay(self):
        raise AssertionError("runtime called HAL_Delay")

    def read_sr2(self, uc, _access, _address, _size, _value, _data):
        if self.busy_reads_left is not None:
            uc.mem_write(0x40005418, struct.pack("<I", 2 if self.busy_reads_left else 0))
            self.busy_reads_left = self.busy_reads_left - 1 if self.busy_reads_left else None

    def i2c(self, write):
        _, wire_addr, reg, memsize = self.args()
        data, size, timeout = struct.unpack("<III", self.read(self.uc.reg_read(UC_ARM_REG_SP), 12))
        addr = wire_addr >> 1
        self.io.append((self.ms, write, addr, reg, size, timeout))
        assert memsize == 1 and timeout == IO_TIMEOUT_MS
        if self.stop_tail_reads:
            # Puya HAL returns before STOP has completed on the physical bus,
            # including the NACK path. Subsequent SR2 reads observe its tail.
            self.busy_reads_left = self.stop_tail_reads
        self.set_ms(self.ms + self.io_delay_ms)
        if addr not in self.devices or reg == self.fail_reg:
            return 1  # HAL_ERROR
        registers = self.devices[addr]
        if write:
            assert size == 1
            registers[reg] = self.read(data, 1)[0]
        else:
            if reg == 0x35:
                payload = struct.pack("<hhh", *self.accel)
            elif reg == 0x3B:
                payload = struct.pack("<hhh", *self.gyro)
            else:
                payload = bytes(registers.get(reg + i, 0) for i in range(size))
            self.uc.mem_write(data, payload[:size])
        return 0

    def send(self):
        index, command, data, size = self.args()
        self.sent.append((index, command, self.read(data, size)))
        return 0

    def event(self):
        _, event, data, size = self.args()
        self.events.append((event, self.read(data, size)))
        return 0

    def scan(self):
        self.call("APP_QmiProbeScan", HANDLE, VECTORS)

    def configure(self):
        self.scan()
        for _ in range(100):
            self.set_ms(self.ms + 20)
            self.call("APP_MotionReadingsUpdate")
            if self.read(self.addr("g_last_probe_result") + 9, 1) == b"\1":
                return
        raise AssertionError("sensor failed to configure")

    def reading(self):
        pointer = self.call("APP_GyroscopeGetReading")
        return struct.unpack("<iii", self.read(pointer, 12)) if pointer else None

    def cal(self, ms, accel=(0, 0, 4096), gyro=(12, -8, 4)):
        self.uc.mem_write(VECTORS, struct.pack("<hhhhhh", *(accel + gyro)))
        self.call("APP_GyroCalibrationUpdate", CAL, ms, VECTORS, VECTORS + 6)

    def count(self):
        return self.read(CAL + 28, 1)[0]

    def phase(self):
        return self.read(CAL + 29, 1)[0]

    def bias(self):
        return struct.unpack("<iii", self.read(CAL, 12))

    def make_service(self):
        # SENSOR_COMMON: vt, service_index, flags, samples, bitfields,
        # interval, api, next_streaming, followed by sample[3], nextSample.
        self.uc.mem_write(SERVICE, struct.pack("<IBBBBIII", 0, 1, 0, 255, 0,
                                               50, self.addr("g_gyroscope_api"), 0) + bytes(16))

    def get(self, command):
        self.uc.mem_write(PACKET, struct.pack("<HBBQBBH", 0, 4, 1, 0, 0, 1, command))
        self.call("gyroscope_handle_packet", SERVICE, PACKET)


class CalibrationTests(unittest.TestCase):
    def setUp(self):
        self.f = Firmware()

    def window(self, start=0, accel=(0, 0, 4096), gyro=(12, -8, 4), count=81):
        # 600ms qualifies stillness; the sample at 600ms starts the bias mean.
        for i in range(30 + count):
            self.f.cal((start + i * 20) & 0xFFFFFFFF, accel, gyro)

    def test_production_stationary_trace_accepts_measured_bias_and_noise(self):
        trace = json.loads((Path(__file__).parent / "stationary_sample_20261021.json").read_text())
        collected = []
        for row in trace["samples"]:
            self.f.cal(row[0], tuple(row[1:4]), tuple(row[4:7]))
            if self.f.count() == 0:
                collected = []
            else:
                collected.append(row)
                self.assertGreaterEqual(row[0], 600)
            if self.f.phase() == 3:
                break
        self.assertEqual(self.f.count(), 81)
        self.assertEqual(len(collected), 81)
        self.assertGreaterEqual(collected[-1][0] - collected[0][0], 1600)
        expected = tuple(math.trunc(sum(row[4+i] for row in collected) * 16384 / 81) for i in range(3))
        self.assertEqual(self.f.bias(), expected)

    def test_handheld_tilt_with_large_bias_breaks_static_window(self):
        for i in range(250):
            angle = math.radians(i * 0.2)  # 10 degrees/s at 50Hz
            accel = (0, round(4096 * math.sin(angle)), round(4096 * math.cos(angle)))
            self.f.cal(i * 20, accel, (480, -90, -30))
            self.assertLess(self.f.phase(), 2)

    def test_qualification_uses_600ms_elapsed_time_before_sampling(self):
        for ms in range(0, 600, 40):
            self.f.cal(ms)
            self.assertEqual(self.f.phase(), 1)
            self.assertEqual(self.f.count(), 0)
        self.f.cal(600)
        self.assertEqual(self.f.phase(), 2)
        self.assertEqual(self.f.count(), 1)

    def test_motion_during_qualification_restarts_full_dwell(self):
        for ms in range(0, 600, 20):
            self.f.cal(ms)
        self.f.cal(600, gyro=(77, -8, 4))
        self.assertEqual((self.f.phase(), self.f.count()), (0, 0))
        for ms in range(620, 1220, 20):
            self.f.cal(ms)
            self.assertEqual((self.f.phase(), self.f.count()), (1, 0))
        self.f.cal(1220)
        self.assertEqual((self.f.phase(), self.f.count()), (2, 1))

    def test_motion_during_sampling_discards_progress_and_requalifies(self):
        self.window(count=40)
        self.assertEqual((self.f.phase(), self.f.count()), (2, 40))
        self.f.cal(1400, accel=(200, 0, 4096))
        self.assertEqual((self.f.phase(), self.f.count()), (0, 0))
        # A new window must collect a new mean, with no old sums included.
        self.window(start=1420, gyro=(20, -10, 8))
        self.assertEqual((self.f.phase(), self.f.count()), (3, 81))
        self.assertEqual(self.f.bias(), (20 * 16384, -10 * 16384, 8 * 16384))

    def test_excessive_rate_at_boot_never_becomes_bias(self):
        self.window(gyro=(1280, 0, 0), count=300)
        self.assertEqual(self.f.count(), 0)

    def test_large_bias_signed_boundaries_do_not_overflow(self):
        self.window(gyro=(1024, -1024, 1024))
        self.assertEqual(self.f.count(), 81)
        self.assertEqual(self.f.bias(), (16777216, -16777216, 16777216))

    def test_rest_calibrates_only_after_full_window_at_any_orientation(self):
        for accel in [(0, 0, 4096), (-4096, 0, 0), (2365, -2365, 2365)]:
            self.f.uc.mem_write(CAL, bytes(32))
            self.window(accel=accel, count=80)
            self.assertEqual(self.f.count(), 80)
            self.f.cal(2200, accel)
            self.assertEqual(self.f.count(), 81)
            self.assertEqual(self.f.bias(), (12 * 16384, -8 * 16384, 4 * 16384))

    def test_bias_average_retains_fractional_raw_count(self):
        for ms in range(0, 600, 20):
            self.f.cal(ms, gyro=(0, 0, 0))
        for i in range(81):
            self.f.cal(600 + i * 20, gyro=(i % 2, -(i % 2), 0))
        mean = 40 * 16384 // 81
        self.assertEqual(self.f.bias(), (mean, -mean, 0))

    def test_gyro_outlier_resets_window(self):
        self.window(count=80)
        self.f.cal(2200, gyro=(1025, 0, 0))
        self.assertEqual(self.f.count(), 0)
        self.window(start=2220, count=80)
        self.assertEqual(self.f.count(), 80)

    def test_gravity_magnitude_and_extreme_values_reject_motion(self):
        for accel in [(0, 0, 0), (0, 0, 5000), (-32768, -32768, -32768)]:
            self.f.uc.mem_write(CAL, bytes(32))
            self.window(count=30)
            self.f.cal(1200, accel=accel)
            self.assertEqual((self.f.phase(), self.f.count()), (0, 0))

    def test_anchor_rejects_accumulated_slow_tilt(self):
        for i in range(42):
            self.f.cal(i * 20, accel=(i * 2, 0, 4096))
        self.assertEqual((self.f.phase(), self.f.count()), (0, 0))

    def test_gyro_change_rejects_candidate(self):
        self.window(count=80)
        self.f.cal(2200, gyro=(77, -8, 4))
        self.assertEqual((self.f.phase(), self.f.count()), (0, 0))

    def test_failure_break_and_long_gap_restart_candidate(self):
        self.window(count=80)
        self.f.call("APP_GyroCalibrationBreak", CAL)
        self.assertEqual((self.f.phase(), self.f.count()), (0, 0))
        self.f.cal(2200)
        self.assertEqual((self.f.phase(), self.f.count()), (1, 0))
        self.f.cal(2261)
        self.assertEqual((self.f.phase(), self.f.count()), (1, 0))
        self.assertEqual(self.f.read(CAL + 30, 2), b"\0\0")

    def test_repeated_polls_cannot_shorten_window(self):
        for ms in range(500):
            self.f.cal(ms)
        self.assertEqual((self.f.phase(), self.f.count()), (1, 0))
        self.assertEqual(int.from_bytes(self.f.read(CAL + 30, 2), "little"), 480)
        for ms in range(500, 601):
            self.f.cal(ms)
        self.assertEqual((self.f.phase(), self.f.count()), (2, 1))

    def test_uint32_tick_wrap_does_not_break_continuity(self):
        self.window(start=0xFFFFFF00)
        self.assertEqual(self.f.count(), 81)

    def test_accepted_bias_survives_motion_and_failures(self):
        self.window()
        before = self.f.bias()
        self.window(start=3000, gyro=(16, -8, 4), count=1000)
        self.f.call("APP_GyroCalibrationBreak", CAL)
        self.assertEqual(self.f.bias(), before)
        self.assertEqual(self.f.count(), 81)


class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.f = Firmware()

    def test_boot_moving_is_reported_without_forced_zero(self):
        self.f.gyro = (640, -1280, 64)
        self.f.configure()
        self.assertEqual(self.f.reading(), (640 * 16384, -1280 * 16384, 64 * 16384))
        for _ in range(100):
            self.f.set_ms(self.f.ms + 20)
            self.f.call("APP_MotionReadingsUpdate")
        self.assertEqual(self.f.read(self.f.addr("g_gyro_calibration") + 28, 1), b"\0")

    def test_stop_tail_busy_does_not_break_probe_or_back_to_back_sampling(self):
        self.f.stop_tail_reads = 5
        self.f.configure()
        self.assertEqual(self.f.reading(), (12 * 16384, -8 * 16384, 4 * 16384))
        for _ in range(110):
            self.f.set_ms(self.f.ms + 20)
            self.f.call("APP_MotionReadingsUpdate")
        self.assertEqual(self.f.reading(), (0, 0, 0))
        self.assertEqual(self.f.read(self.f.addr("g_gyro_calibration") + 28, 1), b"\x51")
        # A config health read is immediately followed by the two vector reads.
        self.f.set_ms(self.f.ms + 1000)
        self.assertIsNotNone(self.f.reading())

    def test_stop_tail_busy_write_waits_without_retrying_transaction(self):
        self.f.stop_tail_reads = 5
        for value in [0x40, 0x41, 0x40]:
            self.assertEqual(self.f.call("APP_QmiProbeWriteReg", HANDLE, 0x6B, 2, value), 0)
        self.assertEqual(len(self.f.io), 3)

    def test_jacdac_interrupt_sized_io_delay_keeps_reading_fresh(self):
        self.f.configure()
        self.f.io_delay_ms = 4  # 100kHz I2C plus a Jacdac frame ISR
        self.f.set_ms(self.f.ms + 20)
        self.assertIsNotNone(self.f.reading())

    def test_full_scale_signed_conversion(self):
        self.f.gyro = (-32768, 32767, -1)
        self.f.configure()
        self.assertEqual(self.f.reading(), (-536870912, 536854528, -16384))

    def test_bias_correction_and_subthreshold_rotation_preserved(self):
        self.f.configure()
        for _ in range(110):
            self.f.set_ms(self.f.ms + 20)
            self.f.call("APP_MotionReadingsUpdate")
        self.assertEqual(self.f.reading(), (0, 0, 0))
        self.f.gyro = (16, -8, 4)  # 0.0625 dps real rotation after calibration
        for _ in range(150):
            self.f.set_ms(self.f.ms + 20)
            self.assertEqual(self.f.reading(), (65536, 0, 0))

    def test_failure_invalidates_output_without_partial_commit_or_busy_retry(self):
        self.f.configure()
        result = self.f.addr("g_last_probe_result")
        old_raw = self.f.read(result + 16, 12)
        self.f.accel = (999, 999, 999)
        self.f.fail_reg = 0x3B
        self.f.set_ms(self.f.ms + 20)
        self.assertIsNone(self.f.reading())
        self.assertEqual(self.f.read(result + 16, 12), old_raw)
        count = len(self.f.io)
        for _ in range(100):
            self.assertIsNone(self.f.reading())
        self.assertEqual(len(self.f.io), count)
        self.assertEqual(self.f.read(self.f.addr("g_gyro_calibration") + 28, 1), b"\0")

    def test_boot_absent_then_device_appears_and_recovers(self):
        self.f.devices = {}
        self.f.scan()
        self.f.set_ms(20)
        self.assertIsNone(self.f.reading())
        count = len(self.f.io)
        for ms in range(40, 500, 20):
            self.f.set_ms(ms)
            self.assertIsNone(self.f.reading())
        self.assertEqual(len(self.f.io), count)
        self.f.devices = {0x6A: {0: 5}}
        for ms in range(500, 2000, 20):
            self.f.set_ms(ms)
            if self.f.reading() is not None:
                break
        else:
            self.fail("late sensor never recovered")
        self.assertEqual(self.f.read(self.f.addr("g_last_probe_result") + 3, 1), b"\x6a")

    def test_mismatched_part_is_never_configured(self):
        self.f.devices = {0x6B: {0: 0xAA}}
        self.f.scan()
        for ms in range(20, 2000, 20):
            self.f.set_ms(ms)
            self.assertIsNone(self.f.reading())
        self.assertFalse(any(op[1] for op in self.f.io))

    def test_selected_address_identity_is_consistent_and_6b_is_preferred(self):
        for second_id, expected_address in [(0xAA, 0x6A), (5, 0x6B)]:
            self.f.devices = {0x6A: {0: 5}, 0x6B: {0: second_id}}
            self.f.configure()
            result = self.f.addr("g_last_probe_result")
            self.assertEqual(self.f.read(result + 3, 3), bytes((expected_address, 5, 1)))

    def test_healthy_periodic_checks_wrap_without_losing_readings(self):
        self.f.configure()
        self.f.io.clear()
        for _ in range(6):
            self.f.set_ms(self.f.ms + 1000)
            self.assertIsNotNone(self.f.reading())
        checks = [op[3] for op in self.f.io if not op[1] and op[4] == 1]
        self.assertEqual(checks, [2, 3, 4, 6, 8, 2])

    def test_config_writes_and_verification_are_spread_and_settle_without_delay(self):
        self.f.configure()
        writes = [op for op in self.f.io if op[1]]
        self.assertEqual([op[3] for op in writes], [2, 3, 4, 6, 8])
        self.assertEqual(len({op[0] for op in writes}), 5)
        first_verify = next(op for op in self.f.io if not op[1] and op[3] == 2)
        self.assertGreaterEqual(first_verify[0] - writes[-1][0], 100)
        self.assertFalse(any(op[3] == 0x33 for op in self.f.io))

    def test_config_readback_mismatch_does_not_publish(self):
        self.f.scan()
        for ms in range(20, 300, 20):
            self.f.set_ms(ms)
            self.f.call("APP_MotionReadingsUpdate")
            if ms == 100:
                self.f.devices[0x6B][2] = 0
        self.assertIsNone(self.f.reading())

    def test_silent_sensor_reset_is_detected_by_periodic_config_check(self):
        self.f.configure()
        self.f.devices[0x6B][2] = 0
        self.f.set_ms(self.f.ms + 1000)
        self.assertIsNone(self.f.reading())

    def test_stuck_bus_skips_hal_wait(self):
        self.f.configure()
        self.f.uc.mem_write(0x40005418, struct.pack("<I", 2))  # I2C SR2 BUSY
        self.f.set_ms(self.f.ms + 20)
        count = len(self.f.io)
        self.assertIsNone(self.f.reading())
        self.assertEqual(len(self.f.io), count)

    def test_unexpected_long_acquisition_is_not_published_as_fresh(self):
        self.f.configure()
        self.f.set_ms(self.f.ms + 20)
        self.f.io_delay_ms = 40
        self.assertIsNone(self.f.reading())
        self.assertEqual(self.f.read(self.f.addr("g_gyro_calibration") + 28, 1), b"\0")

    def test_transient_failure_recovers_and_preserves_trusted_bias(self):
        self.f.configure()
        for _ in range(110):
            self.f.set_ms(self.f.ms + 20)
            self.f.call("APP_MotionReadingsUpdate")
        self.f.fail_reg = 0x3B
        self.f.set_ms(self.f.ms + 20)
        self.assertIsNone(self.f.reading())
        self.f.fail_reg = None
        for _ in range(100):
            self.f.set_ms(self.f.ms + 20)
            reading = self.f.reading()
            if reading is not None:
                self.assertEqual(reading, (0, 0, 0))
                break
        else:
            self.fail("sensor failed to recover")


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.f = Firmware()
        self.f.gyro = (640, -1280, 64)
        self.f.configure()
        self.f.make_service()

    def reading_reports(self):
        return [payload for index, cmd, payload in self.f.sent if index == 1 and cmd == READING]

    def test_stream_and_direct_get_use_same_mapping_and_12_byte_payload(self):
        self.f.call("gyroscope_process", SERVICE)
        self.f.get(READING)
        expected = struct.pack("<iii", 1280 * 16384, -640 * 16384, -64 * 16384)
        self.assertEqual(self.reading_reports(), [expected, expected])

    def test_direct_get_and_stream_suppress_stale_data_and_status_changes(self):
        self.f.call("gyroscope_process", SERVICE)
        self.f.get(STATUS)
        self.assertEqual(self.f.sent[-1], (1, STATUS, struct.pack("<I", 0)))
        count = len(self.reading_reports())
        self.f.fail_reg = 0x3B
        self.f.set_ms(self.f.ms + 60)
        self.f.get(READING)  # failure occurs before next scheduled service process
        self.f.get(STATUS)
        self.assertEqual(self.f.sent[-1], (1, STATUS, struct.pack("<I", 1)))
        self.f.call("gyroscope_process", SERVICE)
        self.assertEqual(len(self.reading_reports()), count)
        self.assertEqual(self.f.events[-1][1], struct.pack("<I", 1))

    def test_healthy_status_recovers_and_reports_resume(self):
        self.f.call("gyroscope_process", SERVICE)
        self.f.fail_reg = 0x35
        self.f.set_ms(self.f.ms + 20)
        self.f.get(READING)
        self.f.fail_reg = None
        for _ in range(100):
            self.f.set_ms(self.f.ms + 20)
            self.f.get(READING)
            if self.f.events[-1][1] == struct.pack("<I", 0):
                break
        else:
            self.fail("READY status was not restored")
        self.f.get(STATUS)
        self.assertEqual(self.f.sent[-1], (1, STATUS, struct.pack("<I", 0)))
        self.assertEqual(len(self.reading_reports()), 2)

    def test_unknown_register_keeps_not_implemented_response(self):
        self.f.get(0x1FEE)
        self.assertTrue(self.f.sent)
        self.assertEqual(self.reading_reports(), [])


if __name__ == "__main__":
    print(f"Firmware: {AXF.name}\nAXF SHA-256: {hashlib.sha256(AXF.read_bytes()).hexdigest()}", flush=True)
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
