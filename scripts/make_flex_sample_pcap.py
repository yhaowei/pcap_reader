#!/usr/bin/env python3
"""Build a PCAP with one FLEX MBO UDP multicast message (protocol_spec.pdf)."""

import struct
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "flex_sample.pcap"

MCAST_MAC = bytes.fromhex("01005e000dc3")
SRC_MAC = bytes.fromhex("001122334455")
DST_IP = bytes([224, 0, 13, 195])  # 224.0.13.195
SRC_IP = bytes([10, 1, 2, 3])
SRC_PORT = 50000
DST_PORT = 51052


def be32(v: int) -> bytes:
    return struct.pack(">I", v)


def build_flex_payload() -> bytes:
    header = bytearray(26)
    header[0] = 52  # multicast group
    header[1] = 0  # reboots
    header[2:6] = be32(10030501)  # sequence
    header[6:18] = b"8697".ljust(12)  # issue code (left-aligned)
    header[18:22] = be32(6000207)  # update number
    header[22] = 0  # packet number
    header[23] = 0  # total packets
    header[24] = 0  # utility flag
    header[25] = 4  # message count

    t_tag = bytes([ord("T")]) + be32(1704068110)
    a_bid = (
        bytes([ord("A")])
        + be32(1000)
        + be32(1001)
        + bytes([ord("B")])
        + (2000).to_bytes(6, "big")
        + (980000).to_bytes(8, "big")
        + bytes([0, 0])
    )
    a_ask = (
        bytes([ord("A")])
        + be32(1001)
        + be32(1002)
        + bytes([ord("S")])
        + (1500).to_bytes(6, "big")
        + (1020000).to_bytes(8, "big")
        + bytes([0, 0])
    )
    e_tag = (
        bytes([ord("E")])
        + be32(1002)
        + be32(1001)
        + bytes([ord("B")])
        + (500).to_bytes(6, "big")
        + be32(1)
    )

    body = bytes(header)
    body += bytes([len(t_tag)]) + t_tag
    body += bytes([len(a_bid)]) + a_bid
    body += bytes([len(a_ask)]) + a_ask
    body += bytes([len(e_tag)]) + e_tag
    return body


def build_udp_ip_frame(payload: bytes) -> bytes:
    udp_len = 8 + len(payload)
    ip_len = 20 + udp_len
    ip_hdr = struct.pack(
        ">BBHHHBBH4s4s",
        0x45,
        0,
        ip_len,
        0x1234,
        0,
        64,
        17,
        0,
        SRC_IP,
        DST_IP,
    )
    udp_hdr = struct.pack(">HHHH", SRC_PORT, DST_PORT, udp_len, 0)
    eth = MCAST_MAC + SRC_MAC + struct.pack(">H", 0x0800)
    return eth + ip_hdr + udp_hdr + payload


def write_pcap(path: Path, frame: bytes) -> None:
    global_hdr = struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1)
    rec_hdr = struct.pack("<IIII", 1_700_000_000, 123456, len(frame), len(frame))
    path.write_bytes(global_hdr + rec_hdr + frame)


def main() -> None:
    frame = build_udp_ip_frame(build_flex_payload())
    write_pcap(OUT, frame)
    print(f"Wrote {OUT} ({len(frame)} byte frame)")


if __name__ == "__main__":
    main()
