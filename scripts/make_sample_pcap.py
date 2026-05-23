#!/usr/bin/env python3
"""Create a minimal PCAP with two small Ethernet frames for testing."""

import struct
from pathlib import Path

PCAP_PATH = Path(__file__).resolve().parent.parent / "sample.pcap"

# Global header: magic, vmaj, vmin, zone, sigfigs, snaplen, network
global_hdr = struct.pack(
    "<IHHIIII",
    0xA1B2C3D4,  # little-endian microsecond timestamps
    2,
    4,
    0,
    0,
    65535,
    1,  # Ethernet
)

# Minimal Ethernet frames (dst, src, ethertype, payload)
frame1 = bytes.fromhex(
    "ffffffffffff 001122334455 0800"
    "4500001c0001000040060000000000000000000000"
)
frame2 = bytes.fromhex(
    "001122334455 aabbccddeeff 0806"
    "0001080006040001001122334455aabbccddeeff"
    "c0a80101c0a80102"
)

packets = [
    (1_700_000_000, 123456, frame1),
    (1_700_000_001, 654321, frame2),
]

with PCAP_PATH.open("wb") as f:
    f.write(global_hdr)
    for ts_sec, ts_usec, data in packets:
        f.write(struct.pack("<IIII", ts_sec, ts_usec, len(data), len(data)))
        f.write(data)

print(f"Wrote {PCAP_PATH}")
