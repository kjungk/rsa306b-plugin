#!/usr/bin/env python3
"""Extract and inspect CRC-checked stored calibration blocks from a USB PCAP.

Offline only: no USB access, DLL execution, calibration writes or dBm conversion.
Supported payload layouts follow the reference build documented in Calibration.md.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from decode_usb import decode

MAX_BLOCK_BYTES = 1024 * 1024


def crc16(data):
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


def describe(block, data):
    """Bounds-check known table layouts; names describe stored data, not output power."""
    def integers(count):
        if len(data) < 4 * count:
            raise ValueError('Truncated table header')
        return struct.unpack_from('<' + 'I' * count, data)
    def array(offset, count, kind='f'):
        width = struct.calcsize(kind)
        if count <= 0 or count > MAX_BLOCK_BYTES // width or offset < header_size or offset + count * width > len(data):
            raise ValueError('Table array outside payload')
        values = list(struct.unpack_from('<' + str(count) + kind, data, offset))
        if not all(math.isfinite(x) for x in values):
            raise ValueError('Non-finite calibration data')
        return values
    if block not in (2, 3, 4, 5, 6, 8):
        return dict(layout='not interpreted')
    revision = integers(1)[0]
    if revision != (1 if block in (5, 6, 8) else 0):
        raise ValueError('Unsupported calibration table revision')
    result = dict(revision=revision, interpreted=True, calibrated=False)
    if block == 3:
        header_size = 16
        _, count, x, y = integers(4)
        if count != 32:
            raise ValueError('Unsupported IF VGA table size')
        result.update(kind='if-vga', settings=array(x, count), measured_values=array(y, count))
    elif block == 4:
        header_size = 24
        _, rows, cols, x, frequency, values = integers(6)
        result.update(kind='rf-attenuator', settings=array(x, rows), frequency_hz=array(frequency, cols, 'd'),
                      measured_rows=[array(values + i * cols * 4, cols) for i in range(rows)] if 0 < rows <= 256 else [])
        if not result['measured_rows']:
            raise ValueError('Unsupported RF attenuator row count')
    elif block == 5:
        header_size = 40
        _, descriptors, bands, frequency, count, values, rows, row_count, _, size = integers(10)
        if rows != 4 or row_count != count or not 0 < bands <= 64 or descriptors < header_size or descriptors + bands * 40 > len(data) or size != len(data):
            raise ValueError('Invalid RF gain dimensions')
        ranges = [struct.unpack_from('<dd', data, descriptors + i * 40) for i in range(bands)]
        if not all(math.isfinite(a) and math.isfinite(b) and a < b for a, b in ranges):
            raise ValueError('Invalid RF descriptor bounds')
        result.update(kind='rf-path-gain', descriptor_ranges_hz=ranges, frequency_hz=array(frequency, count, 'd'),
                      gain_rows=[array(values + i * count * 4, count) for i in range(rows)])
    elif block == 2:
        header_size = 36
        _, count, rows, cols, settings, measurements, ids, frequency, values = integers(9)
        if rows != 2:
            raise ValueError('Unsupported LF path row count')
        # VGA identifiers are integers, not floating-point calibration values.
        if ids < header_size or ids + 4 * rows > len(data):
            raise ValueError('LF VGA identifiers outside payload')
        result.update(kind='lf-path-gain', settings=array(settings, count), measured_values=array(measurements, count),
                      vga_ids=list(struct.unpack_from('<2I', data, ids)), frequency_hz=array(frequency, cols, 'd'),
                      measured_rows=[array(values + i * cols * 4, cols) for i in range(rows)])
    else:
        header_size = 20
        _, rows, cols, descriptors, values = integers(5)
        if not 0 < rows <= 64 or descriptors < header_size or descriptors + rows * 40 > len(data):
            raise ValueError('IF response descriptors outside payload')
        result.update(kind='if-response' if block == 6 else 'normalized-if-response',
                      descriptor_hex=[data[descriptors+i*40:descriptors+(i+1)*40].hex() for i in range(rows)],
                      response_rows=[array(values+i*cols*4, cols) for i in range(rows)])
    if 'frequency_hz' in result and not all(a < b for a, b in zip(result['frequency_hz'], result['frequency_hz'][1:])):
        raise ValueError('Non-increasing calibration frequency axis')
    return result


class BlockCollector:
    def __init__(self):
        self.blocks = {}
        self.device = None

    def write(self, line):
        record = json.loads(line)
        request, response = record['request'], record['response']
        opcode = request['opcode']
        if opcode not in (17, 19):
            return
        device = (record['bus'], record['device'])
        if self.device is not None and device != self.device:
            raise ValueError('Select one USB device for calibration inspection')
        self.device = device
        if response.get('status') or response.get('auxiliary'):
            raise ValueError('Failed stored-block command')
        a, b = bytes.fromhex(request['payload_hex']), bytes.fromhex(response['payload_hex'])
        if opcode == 17:
            if len(a) != 2 or len(b) != 8:
                raise ValueError('Invalid block header envelope')
            block, = struct.unpack('<H', a)
            crc, size = struct.unpack_from('<HI', b)
            if not 0 < size <= MAX_BLOCK_BYTES:
                raise ValueError('Invalid stored-block size')
            previous = self.blocks.get(block)
            if previous is None and (len(self.blocks) >= 32 or sum(s['size'] for s in self.blocks.values()) + size > 16 * MAX_BLOCK_BYTES):
                raise ValueError('Stored-block collection exceeds memory limit')
            if previous is not None and (previous['crc'], previous['size']) != (crc, size):
                raise ValueError('Stored block changed during capture')
            self.blocks.setdefault(block, dict(crc=crc, size=size, pages={}))
        else:
            if len(a) != 4 or len(b) != 256:
                raise ValueError('Invalid block page envelope')
            block, page = struct.unpack('<HH', a)
            state = self.blocks.get(block)
            if state is None or page >= (state['size'] + 255) // 256:
                raise ValueError('Unexpected stored-block page')
            if page in state['pages'] and state['pages'][page] != b:
                raise ValueError('Conflicting stored-block page')
            state['pages'][page] = b

    def payloads(self):
        for block, state in sorted(self.blocks.items()):
            count = (state['size'] + 255) // 256
            if len(state['pages']) != count:
                raise ValueError(f'Incomplete stored block {block}')
            data = b''.join(state['pages'][i] for i in range(count))[:state['size']]
            if crc16(data) != state['crc']:
                raise ValueError(f'CRC mismatch for stored block {block}')
            yield block, state['crc'], data


def native_payloads(path):
    """Validate a bounded read-calibration JSON dump before interpreting tables."""
    if path.stat().st_size > 1024*1024:
        raise ValueError('Oversized native calibration dump')
    dump = json.loads(path.read_text())
    if (not isinstance(dump, dict) or dump.get('schema_version') != 1 or dump.get('type') != 'calibration_dump' or
            dump.get('calibrated') is not False or dump.get('model') != 'RSA306B' or
            (dump.get('hardware'), dump.get('firmware_word'), dump.get('fpga_word')) != (7, 0x701, 0x201)):
        raise ValueError('Unsupported native calibration dump')
    limits = {0:4096, 1:4096, 2:4096, 3:4096, 4:65536, 5:32768, 6:8192, 8:8192}
    blocks = dump.get('blocks')
    if not isinstance(blocks, list) or len(blocks) != len(limits):
        raise ValueError('Incomplete native calibration dump')
    result, seen = [], set()
    for row in blocks:
        if not isinstance(row, dict):
            raise ValueError('Invalid native block record')
        index, size, crc, hex_data = (row.get(k) for k in ('index', 'bytes', 'crc16', 'payload_hex'))
        if (type(index) is not int or index not in limits or index in seen or type(size) is not int or
                not 0 < size <= limits[index] or type(crc) is not int or not 0 <= crc <= 65535 or
                not isinstance(hex_data, str) or len(hex_data) != size*2):
            raise ValueError('Invalid native stored-block envelope')
        data = bytes.fromhex(hex_data)
        if len(data) != size or crc16(data) != crc:
            raise ValueError('Native stored-block CRC/length mismatch')
        seen.add(index)
        result.append((index, crc, data))
    identity = next(data for index, _, data in result if index == 0)
    if len(identity) < 512 or identity[:256].split(b'\0', 1)[0] != b'RSA306B':
        raise ValueError('Native identity block mismatch')
    serial = identity[256:512].split(b'\0', 1)[0].decode('ascii')
    if not serial or serial != dump.get('serial'):
        raise ValueError('Native serial does not match identity block')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument('--pcap', type=Path)
    source.add_argument('--native-json', type=Path, help='JSON from rsa306 read-calibration')
    p.add_argument('--device', type=int)
    p.add_argument('--output', required=True, type=Path, help='New local directory; retain outside Git')
    args = p.parse_args()
    if args.output.exists():
        p.error('Output directory already exists')
    if args.native_json and args.device is not None:
        p.error('--device applies only to PCAP input')
    entries = []
    if args.native_json:
        payloads = native_payloads(args.native_json)
    else:
        collector = BlockCollector()
        decode(args.pcap, device=args.device, command_sink=collector)
        payloads = list(collector.payloads())
    if not payloads:
        p.error('No complete stored-block reads in capture')
    for block, crc, data in payloads:
        entries.append(dict(block=block, bytes=len(data), crc16=f'{crc:04x}',
                            sha256=hashlib.sha256(data).hexdigest(), table=describe(block, data)))
    # Only create output after every block and known table layout has validated.
    args.output.mkdir(parents=True, exist_ok=False)
    for block, _, data in payloads:
        (args.output/f'block-{block}.bin').write_bytes(data)
    source_path = args.pcap or args.native_json
    with source_path.open('rb') as stream:
        hasher = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            hasher.update(chunk)
        digest = hasher.hexdigest()
    (args.output/'tables.json').write_text(json.dumps(dict(schema_version=1,
        basis='captured stored data; interpretation not independently calibrated', calibrated=False,
        source_kind='usbpcap' if args.pcap else 'native-json', source_sha256=digest,
        **({'pcap_sha256':digest} if args.pcap else {'native_json_sha256':digest}),
        blocks=entries), allow_nan=False, indent=2)+'\n')
    print(json.dumps(dict(blocks=len(entries), output=str(args.output), calibrated=False)))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as error:
        print(f'Calibration inspection failed: {error}', file=sys.stderr)
        raise SystemExit(1)
