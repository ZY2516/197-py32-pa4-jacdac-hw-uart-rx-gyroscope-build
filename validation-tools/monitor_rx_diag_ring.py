import argparse
import json
import re
import struct
import time
from pathlib import Path

RING_RECORD_SIZE = 44
RING_RECORD_COUNT = 4
DEFAULT_MAP = Path(__file__).resolve().parents[1] / (
    "MDK-ARM/Output/py32f002b_qmi8656_jacdac_clean_hybrid_phy_build.map"
)
COUNTER_SYMBOLS = {
    "rx_complete": "g_app_jd_diag_rx_complete",
    "crc_ok": "g_app_jd_diag_crc_ok",
    "control_packet": "g_app_jd_diag_control_packet",
    "product_id_get": "g_app_jd_diag_product_id_get",
    "rxne": "g_app_jd_rx_hw_rxne_count",
    "fe": "g_app_jd_rx_hw_fe_count",
    "ne": "g_app_jd_rx_hw_ne_count",
    "ore": "g_app_jd_rx_hw_ore_count",
    "quiet_rearm": "g_app_jd_rx_hw_quiet_rearm_count",
    "quiet_guard_abort": "g_app_jd_rx_hw_quiet_guard_abort_count",
}


def read_symbols(map_path: Path) -> dict:
    """Require the MAP matching the firmware on the device; addresses can move."""
    symbols = {
        name: int(address, 16)
        for name, address in re.findall(
            r"^\s*(\w+)\s+0x([0-9a-fA-F]+)\s+Data\s", map_path.read_text(), re.MULTILINE
        )
    }
    required = set(COUNTER_SYMBOLS.values()) | {
        "g_app_jd_rx_diag_ring_head", "g_app_jd_rx_diag_ring"
    }
    missing = required - symbols.keys()
    if missing:
        raise ValueError(f"Missing diagnostic symbols in {map_path}: {sorted(missing)}")
    return symbols


def decode_record(slot: int, data: bytes) -> dict:
    return {
        "slot": slot,
        "seq": struct.unpack_from("<I", data, 0)[0],
        "data_left": struct.unpack_from("<i", data, 4)[0],
        "tx_size": struct.unpack_from("<I", data, 8)[0],
        "declared_size": struct.unpack_from("<I", data, 12)[0],
        "calc_crc": struct.unpack_from("<H", data, 16)[0],
        "frame_crc": struct.unpack_from("<H", data, 18)[0],
        "frame_size": data[20],
        "flags": data[21],
        "service_size": data[22],
        "service_index": data[23] & 0x3F,
        "service_command": struct.unpack_from("<H", data, 24)[0],
        "reason": data[26],
        "raw_len": data[27],
        "raw_hex": data[28:44].hex(),
    }


def read_counters(target, symbols) -> dict:
    return {key: target.read32(symbols[name]) for key, name in COUNTER_SYMBOLS.items()}


def main() -> None:
    from pyocd.core.helpers import ConnectHelper
    from pyocd.core.target import Target

    parser = argparse.ArgumentParser()
    parser.add_argument("--pack", required=True)
    parser.add_argument("--ready", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--duration", type=float, default=8.0)
    parser.add_argument("--uid", default="006000133900001456314D43")
    parser.add_argument("--map", type=Path, default=DEFAULT_MAP,
                        help="Keil MAP matching the firmware currently flashed on the device")
    args = parser.parse_args()
    symbols = read_symbols(args.map)
    ring_head_address = symbols["g_app_jd_rx_diag_ring_head"]
    ring_address = symbols["g_app_jd_rx_diag_ring"]

    ready_path = Path(args.ready)
    output_path = Path(args.output)
    ready_path.unlink(missing_ok=True)
    output_path.unlink(missing_ok=True)

    session = ConnectHelper.session_with_chosen_probe(
        unique_id=args.uid,
        options={
            "pack": args.pack,
            "target_override": "py32f002bx5",
            "frequency": 100000,
            "connect_mode": "attach",
            "resume_on_disconnect": True,
        },
    )
    if session is None:
        raise RuntimeError("ST-Link session was not created")

    records = {}
    with session:
        target = session.board.target
        if target.get_state() == Target.State.HALTED:
            target.resume()

        initial_head = target.read32(ring_head_address)
        before = read_counters(target, symbols)
        ready_path.write_text(
            json.dumps({"initial_head": initial_head, "before": before}),
            encoding="utf-8",
        )

        started = time.monotonic()
        while time.monotonic() - started < args.duration:
            ring = bytes(target.read_memory_block8(
                ring_address, RING_RECORD_SIZE * RING_RECORD_COUNT
            ))
            committed_head = target.read32(ring_head_address)
            for slot in range(RING_RECORD_COUNT):
                offset = slot * RING_RECORD_SIZE
                record = decode_record(slot, ring[offset:offset + RING_RECORD_SIZE])
                seq = record["seq"]
                if initial_head < seq <= committed_head:
                    record["observed_ms"] = round((time.monotonic() - started) * 1000, 3)
                    records.setdefault(seq, record)
            time.sleep(0.01)

        after = read_counters(target, symbols)
        final_head = target.read32(ring_head_address)
        if target.get_state() == Target.State.HALTED:
            target.resume()

    output_path.write_text(
        json.dumps(
            {
                "initial_head": initial_head,
                "final_head": final_head,
                "before": before,
                "after": after,
                "records": [records[key] for key in sorted(records)],
            },
            indent=2,
        ),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
