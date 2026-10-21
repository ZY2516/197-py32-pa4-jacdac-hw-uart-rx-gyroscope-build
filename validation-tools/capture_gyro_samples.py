"""Record coherent completed samples through SWD without halting the target."""
import argparse
import io
import json
import struct
import time
from pathlib import Path

from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "MDK-ARM/Output/py32f002b_qmi8656_jacdac_clean_hybrid_phy_build"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uid", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=12)
    parser.add_argument("--reset", action="store_true", help="Restart after verifying Flash, to capture initial calibration")
    args = parser.parse_args()
    elf = ELFFile(io.BytesIO(IMAGE.with_suffix(".axf").read_bytes()))
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols() if s.name}
    samples = []
    with ConnectHelper.session_with_chosen_probe(unique_id=args.uid, options={
        "pack": str(ROOT / "validation-tools/Puya.PY32F0xx_DFP.1.2.19.pack"),
        "target_override": "py32f002bx5", "frequency": 1000000,
        "connect_mode": "attach", "resume_on_disconnect": True,
    }) as session:
        target = session.board.target
        # Prove the ELF describes the live image before interpreting its SRAM.
        for section in elf.iter_sections():
            if section["sh_flags"] & 2 and 0x08000000 <= section["sh_addr"] < 0x08006000:
                actual = bytes(target.read_memory_block8(section["sh_addr"], section["sh_size"]))
                if actual != section.data():
                    raise RuntimeError("Flash does not match the ELF")
        if args.reset:
            target.reset()
            target.flush()
            time.sleep(0.05)  # allow startup to clear the preceding run's SRAM
        started = time.monotonic()
        previous = None
        while time.monotonic() - started < args.duration:
            stamp = target.read32(symbols["g_last_gyro_attempt_ms"])
            if stamp == previous or not target.read8(symbols["g_gyro_sample_valid"]):
                time.sleep(0.003)
                continue
            raw = bytes(target.read_memory_block8(symbols["g_last_probe_result"], 30))
            cal = bytes(target.read_memory_block8(symbols["g_gyro_calibration"], 32))
            reading = bytes(target.read_memory_block8(symbols["g_gyro_reading"], 12))
            if not target.read8(symbols["g_gyro_sample_valid"]) or target.read32(symbols["g_last_gyro_attempt_ms"]) != stamp:
                continue
            previous = stamp
            samples.append({"ms": stamp, "accel": struct.unpack_from("<hhh", raw, 16),
                            "gyro": struct.unpack_from("<hhh", raw, 22), "cal_count": cal[28],
                            "cal_phase": cal[29], "still_elapsed_ms": struct.unpack_from("<H", cal, 30)[0],
                            "sum_or_bias": struct.unpack_from("<iii", cal),
                            "reading": struct.unpack("<iii", reading)})
    args.output.write_text(json.dumps(samples, indent=2), encoding="utf-8")
    print(json.dumps({"samples": len(samples), "duration": args.duration,
                      "first": samples[:1], "last": samples[-1:]}))


if __name__ == "__main__":
    main()
