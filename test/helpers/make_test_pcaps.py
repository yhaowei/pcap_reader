#!/usr/bin/env python3
"""Generate small PCAP files for unit tests."""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "gui"))

from tse.pcap_loader import read_pcap_file


def write_pcap(path: Path, ts_sec: int, ts_frac: int, frame: bytes, nano: bool = False) -> None:
    magic = 0x4D3CB2A1 if nano else 0xD4C3B2A1
    global_hdr = struct.pack("<IHHIIII", magic, 2, 4, 0, 0, 65535, 1)
    rec_hdr = struct.pack("<IIII", ts_sec, ts_frac, len(frame), len(frame))
    path.write_bytes(global_hdr + rec_hdr + frame)


def minimal_udp_frame(payload: bytes) -> bytes:
    dst_mac = bytes.fromhex("01005e000dc3")
    src_mac = bytes.fromhex("001122334455")
    eth = dst_mac + src_mac + struct.pack(">H", 0x0800)
    dst_ip = bytes([224, 0, 13, 195])
    src_ip = bytes([10, 1, 2, 3])
    ip_hdr = struct.pack(
        ">BBHHHBBH4s4s",
        0x45,
        0,
        20 + 8 + len(payload),
        0,
        0,
        64,
        17,
        0,
        src_ip,
        dst_ip,
    )
    udp_hdr = struct.pack(">HHHH", 50000, 51052, 8 + len(payload), 0)
    return eth + ip_hdr + udp_hdr + payload


if __name__ == "__main__":
    out = Path(__file__).resolve().parent.parent / "fixtures"
    out.mkdir(parents=True, exist_ok=True)
    payload = bytes(26) + bytes([0])  # maintenance-style empty
    write_pcap(out / "early.pcap", 100, 100, minimal_udp_frame(payload))
    write_pcap(out / "late.pcap", 200, 200, minimal_udp_frame(payload))
    print("Wrote", out / "early.pcap", out / "late.pcap")
