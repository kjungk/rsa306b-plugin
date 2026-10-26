#!/usr/bin/env python3
"""Validate classic USBPcap PCAP framing and summarize transfers; never replay USB.

USB header source: https://desowin.org/usbpcap/captureformat.html
Only LINKTYPE_USBPCAP (249), PCAP 2.4, and micro/nanosecond timestamps are accepted.
A clean file does not prove the capture driver lost no packets.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct
import sys


def packets(path: Path):
    with path.open("rb") as stream:
        header = stream.read(24)
        formats = {b"\xd4\xc3\xb2\xa1": ("<", 1000), b"\xa1\xb2\xc3\xd4": (">", 1000),
                   b"\x4d\x3c\xb2\xa1": ("<", 1), b"\xa1\xb2\x3c\x4d": (">", 1)}
        if len(header) != 24 or header[:4] not in formats:
            raise ValueError("Not a supported classic PCAP file")
        endian, scale = formats[header[:4]]
        major, minor, _, _, snaplen, linktype = struct.unpack(endian + "HHiiII", header[4:])
        if (major, minor, linktype) != (2, 4, 249):
            raise ValueError("Require PCAP 2.4 LINKTYPE_USBPCAP (249)")
        number = 0
        while record := stream.read(16):
            number += 1
            if len(record) != 16:
                raise ValueError(f"Incomplete PCAP record header at packet {number}")
            sec, subsec, included, original = struct.unpack(endian + "IIII", record)
            if included > original or included > snaplen or included > 128 * 1024 * 1024:
                raise ValueError(f"Invalid/oversized captured length at packet {number}")
            data = stream.read(included)
            if len(data) != included:
                raise ValueError(f"Incomplete PCAP record body at packet {number}")
            if included < 27:
                raise ValueError(f"Short USBPcap header at packet {number}")
            hlen, irp, status, function, info, bus, device, ep, transfer, length = struct.unpack_from("<HQIHBHHBBI", data)
            if not 27 <= hlen <= included:
                raise ValueError(f"Invalid USBPcap header length at packet {number}")
            if transfer == 2 and hlen < 28:
                raise ValueError(f"Missing control-stage header at packet {number}")
            yield {"number": number, "unix_ns": sec * 1_000_000_000 + subsec * scale,
                   "included": included, "original": original, "snaplen": snaplen,
                   "irp": irp, "status": status, "function": function, "info": info,
                   "bus": bus, "device": device, "endpoint": ep, "transfer": transfer,
                   "data_length": length, "payload": data[hlen:],
                   "stage": data[27] if transfer == 2 else None}


def inspect(path: Path):
    counts, sizes, statuses, controls, devices = Counter(), defaultdict(Counter), Counter(), Counter(), Counter()
    total, truncated, mismatches, max_record, first, last = 0, 0, 0, 0, None, None
    for packet in packets(path):
        total += 1
        truncated += packet["included"] < packet["original"]
        mismatches += len(packet["payload"]) != packet["data_length"]
        max_record = max(max_record, packet["original"])
        first = packet["unix_ns"] if first is None else min(first, packet["unix_ns"])
        last = packet["unix_ns"] if last is None else max(last, packet["unix_ns"])
        direction = "complete" if packet["info"] & 1 else "submit"
        key = f"bus{packet['bus']}:dev{packet['device']}:ep{packet['endpoint']:02x}:type{packet['transfer']}:{direction}"
        counts[key] += 1
        sizes[key][len(packet["payload"])] += 1
        devices[f"{packet['bus']}:{packet['device']}"] += 1
        if packet["info"] & 1 and packet["status"]:
            statuses[f"0x{packet['status']:08x}"] += 1
        if packet["transfer"] == 2 and packet["stage"] == 0 and len(packet["payload"]) >= 8:
            bm, request, value, index, length = struct.unpack_from("<BBHHH", packet["payload"])
            controls[f"{bm:02x}:{request:02x}:value{value:04x}:index{index:04x}:length{length}"] += 1
    return {"schema_version": 1, "path": str(path), "packets": total,
            "truncated_packets": truncated, "payload_length_mismatches": mismatches,
            "max_original_record_bytes": max_record,
            "first_unix_ns": str(first) if first is not None else None,
            "last_unix_ns": str(last) if last is not None else None,
            "duration_seconds": (last - first) / 1e9 if first is not None else None,
            "devices": dict(devices), "transfer_counts": dict(sorted(counts.items())),
            "payload_sizes": {key: dict(sorted(hist.items())) for key, hist in sorted(sizes.items())},
            "nonzero_completion_statuses": dict(statuses), "control_setups": dict(sorted(controls.items())),
            "file_framing_valid": total > 0 and truncated == 0 and mismatches == 0,
            "capture_drops": "unknown; PCAP framing cannot establish capture loss"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pcap", type=Path)
    args = parser.parse_args()
    try:
        result = inspect(args.pcap)
        print(json.dumps(result, indent=2))
        return 0 if result["file_framing_valid"] else 1
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
