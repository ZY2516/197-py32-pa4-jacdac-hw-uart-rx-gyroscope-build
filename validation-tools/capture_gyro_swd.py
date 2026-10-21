"""Attach without reset, back up Flash, and capture running firmware state."""
import argparse
import hashlib
import io
import json
import struct
import time
from pathlib import Path

from elftools.elf.elffile import ELFFile
from intelhex import IntelHex
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "MDK-ARM/Output/py32f002b_qmi8656_jacdac_clean_hybrid_phy_build"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uid", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=12)
    parser.add_argument("--frequency", type=int, default=100000)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    elf = ELFFile(io.BytesIO(IMAGE.with_suffix(".axf").read_bytes()))
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols() if s.name}
    expected = IntelHex(str(IMAGE.with_suffix(".hex")))
    session = ConnectHelper.session_with_chosen_probe(unique_id=args.uid, options={
        "pack": str(ROOT / "validation-tools/Puya.PY32F0xx_DFP.1.2.19.pack"),
        "target_override": "py32f002bx5", "frequency": args.frequency,
        "connect_mode": "attach", "resume_on_disconnect": True,
    })
    report = {"uid": args.uid, "samples": []}
    with session:
        target = session.board.target
        report["initial_state"] = str(target.get_state())
        flash = bytes(target.read_memory_block8(0x08000000, 0x6000))
        # Never overwrite an earlier physical backup.
        with (args.output / "flash.bin").open("xb") as stream:
            stream.write(flash)
        report["flash_sha256"] = hashlib.sha256(flash).hexdigest()
        report["image_matches"] = all(flash[a - 0x08000000] == expected[a] for a in expected.addresses())
        report["axf_sha256"] = hashlib.sha256(IMAGE.with_suffix(".axf").read_bytes()).hexdigest()
        print(json.dumps({k: v for k, v in report.items() if k != "samples"}), flush=True)
        if not report["image_matches"]:
            raise RuntimeError("Flash differs from local HEX; do not use its symbol addresses")

        def snapshot():
            ram = bytes(target.read_memory_block8(0x20000000, 0xC00))
            def data(name, size):
                offset = symbols[name] - 0x20000000
                return ram[offset:offset + size]
            def number(name, size=4):
                return int.from_bytes(data(name, size), "little")
            raw = data("g_last_probe_result", 30)
            names = ["ready", "match", "mismatch", "addr", "part_id", "id_valid",
                     "config_attempted", "write_ok", "config_ok", "motion_ok"]
            probe = dict(zip(names, raw[:10]))
            probe.update(status=raw[28], accel=struct.unpack_from("<hhh", raw, 16),
                         gyro=struct.unpack_from("<hhh", raw, 22))
            result = {"seconds": round(time.monotonic() - started, 3), "probe": probe,
                      "valid": number("g_gyro_sample_valid", 1),
                      "stage": number("g_config_stage", 1),
                      "cal_count": data("g_gyro_calibration", 32)[28],
                      "cal_phase": data("g_gyro_calibration", 32)[29],
                      "still_elapsed_ms": struct.unpack_from("<H", data("g_gyro_calibration", 32), 30)[0],
                      "reading": struct.unpack("<iii", data("g_gyro_reading", 12))}
            for name in ["uwTick", "now", "g_heap_used", "g_irq_lock_depth",
                         "g_app_jd_diag_rx_complete", "g_app_jd_diag_crc_ok",
                         "nextAnnounce", "lastMax",
                         "g_app_jd_rx_hw_rxne_count", "g_app_jd_rx_hw_fe_count",
                         "g_app_jd_rx_hw_ore_count"]:
                result[name] = number(name)
            result["packets_since_announce"] = number("packets_sent", 1)
            # Streaming sample in the allocated service, after the axis mapping.
            service_array = number("services") - 0x20000000
            service = struct.unpack_from("<I", ram, service_array + 4)[0]
            service_offset = service - 0x20000000
            result["service_sample"] = struct.unpack_from("<iii", ram, service_offset + 20)
            result["runtime_flags"] = {name: number(name, 1) for name in
                ["g_runtime_ready", "g_uart_rx_active", "g_uart_tx_active", "g_tx_complete_pending"]}
            return result

        started = time.monotonic()
        while time.monotonic() - started < args.duration:
            sample = snapshot()
            report["samples"].append(sample)
            print(json.dumps(sample), flush=True)
            time.sleep(0.25)
        # Briefly halt only after the undisturbed observation, then resume.
        was_running = target.get_state() != Target.State.HALTED
        try:
            target.halt()
            report["core"] = {name: target.read_core_register(name) for name in
                              ["pc", "lr", "sp", "xpsr", "primask"]}
            report["i2c"] = {hex(offset): target.read32(0x40005400 + offset) for offset in
                             [0, 4, 0x18, 0x14, 0x1C, 0x20]}
            report["ram_hex"] = bytes(target.read_memory_block8(0x20000000, 0xC00)).hex()
        finally:
            if was_running:
                target.resume()
    (args.output / "capture.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"core": report.get("core"), "i2c": report.get("i2c")}))


if __name__ == "__main__":
    main()
