import argparse
import json
import struct
import time
from pathlib import Path

from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target


RING_HEAD_ADDRESS = 0x20000100
RING_ADDRESS = 0x20000618
RING_RECORD_SIZE = 44
RING_RECORD_COUNT = 4


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


def read_counters(target) -> dict:
    words = target.read_memory_block32(0x2000003C, 4)
    uart = target.read_memory_block32(0x200000CC, 7)
    return {
        "rx_complete": words[0],
        "crc_ok": words[1],
        "control_packet": words[2],
        "product_id_get": words[3],
        "rxne": uart[0],
        "fe": uart[1],
        "ne": uart[2],
        "ore": uart[3],
        "quiet_rearm": uart[5],
        "quiet_guard_abort": uart[6],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pack", required=True)
    parser.add_argument("--ready", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--duration", type=float, default=8.0)
    parser.add_argument("--uid", default="006000133900001456314D43")
    args = parser.parse_args()

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

        initial_head = target.read32(RING_HEAD_ADDRESS)
        before = read_counters(target)
        ready_path.write_text(
            json.dumps({"initial_head": initial_head, "before": before}),
            encoding="utf-8",
        )

        started = time.monotonic()
        while time.monotonic() - started < args.duration:
            ring = bytes(target.read_memory_block8(
                RING_ADDRESS, RING_RECORD_SIZE * RING_RECORD_COUNT
            ))
            committed_head = target.read32(RING_HEAD_ADDRESS)
            for slot in range(RING_RECORD_COUNT):
                offset = slot * RING_RECORD_SIZE
                record = decode_record(slot, ring[offset:offset + RING_RECORD_SIZE])
                seq = record["seq"]
                if initial_head < seq <= committed_head:
                    record["observed_ms"] = round((time.monotonic() - started) * 1000, 3)
                    records.setdefault(seq, record)
            time.sleep(0.01)

        after = read_counters(target)
        final_head = target.read32(RING_HEAD_ADDRESS)
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
