#!/usr/bin/env python3
"""Replay a hash-inventoried private RF campaign through the current native math.

No USB access. Reports numerical equivalence only, never hardware qualification
or absolute RF accuracy. The campaign must contain per-run samples.if,
spectra.jsonl and usb.log files covered by its evidence manifest. Calibration
blocks are checked byte-for-byte against the recorded stored-block exchanges.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile

from inspect_calibration import BlockCollector


def digest(path):
    hasher = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            hasher.update(chunk)
    return hasher.hexdigest()


def captured_blocks(path):
    collector = BlockCollector()
    pending = None
    for line in path.read_text().splitlines():
        direction, text = line.split()
        packet = bytes.fromhex(text)
        if len(packet) < 8 or struct.unpack_from('<H', packet, 2)[0] != len(packet) or packet[-2:] != b'\xff\xff':
            raise ValueError('Invalid native command envelope')
        if direction == 'OUT':
            if pending is not None:
                raise ValueError('Unpaired native request')
            pending = packet
        elif direction == 'IN':
            if pending is None or packet[:2] != pending[:2] or packet[4:6] != pending[4:6] or len(packet) < 12:
                raise ValueError('Unpaired native response')
            collector.write(json.dumps(dict(bus=0, device=0,
                request=dict(opcode=struct.unpack_from('<H', pending, 4)[0], payload_hex=pending[6:-2].hex()),
                response=dict(status=struct.unpack_from('<H', packet, 6)[0], auxiliary=struct.unpack_from('<H', packet, 8)[0], payload_hex=packet[10:-2].hex()))))
            pending = None
        else:
            raise ValueError('Unknown command direction')
    if pending is not None:
        raise ValueError('Unanswered native request')
    return {index: data for index, _, data in collector.payloads()}


def spectrum_records(path):
    with path.open() as stream:
        for line in stream:
            row = json.loads(line)
            if row.get('type') == 'spectrum':
                yield row


def compare(reference, actual):
    levels = actual['levels']
    if not levels or len(levels) != len(reference['levels']) or len(levels) != len(reference['frequency_hz']):
        raise ValueError('Mismatched replay point count')
    if not all(math.isfinite(value) for value in levels + reference['levels'] + reference['frequency_hz']):
        raise ValueError('Non-finite spectrum')
    axis = max(abs(actual['start_hz'] + i * actual['step_hz'] - hz) for i, hz in enumerate(reference['frequency_hz']))
    enbw = abs(actual['enbw_hz'] - reference['window_enbw_hz'])
    error = max(abs(a - b) for a, b in zip(levels, reference['levels']))
    if not math.isfinite(axis + enbw + error):
        raise ValueError('Non-finite comparison')
    return axis, enbw, error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--tables', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output must be new')
    root = args.manifest.resolve().parent
    manifest = json.loads(args.manifest.read_text())
    inventory = {entry['path']: entry for entry in manifest['files']}
    runs = sorted({str(Path(path).parent) for path in inventory if path.endswith('/samples.if')})
    if not runs:
        raise ValueError('No raw sample recordings in inventory')
    tables = {i: (args.tables / f'block-{i}.bin').read_bytes() for i in range(2, 7)}
    results = []
    for run in runs:
        directory = root / run
        if not directory.resolve().is_relative_to(root):
            raise ValueError('Recording path escapes evidence root')
        hashes = {}
        for name in ('samples.if', 'spectra.jsonl', 'usb.log'):
            entry = inventory[f'{run}/{name}']
            path = directory / name
            hashes[name] = digest(path)
            if path.stat().st_size != entry['bytes'] or hashes[name] != entry['sha256']:
                raise ValueError(f'Changed evidence: {run}/{name}')
        blocks = captured_blocks(directory / 'usb.log')
        if any(blocks.get(i) != tables[i] for i in tables):
            raise ValueError(f'Calibration tables differ from recorded device: {run}')
        with tempfile.TemporaryDirectory(prefix='rsa-rf-replay-') as temporary:
            settings_path = Path(temporary) / 'settings.txt'
            count = 0
            with settings_path.open('w') as settings:
                for row in spectrum_records(directory / 'spectra.jsonl'):
                    if row.get('level_unit') != 'dBm' or row.get('calibrated') is not True or row.get('receiver_path') != 'heterodyne-rf' or row.get('rf_path_bits') != 0x71:
                        raise ValueError('Expected factory RF dBm recording')
                    if row['sequence'] != count:
                        raise ValueError('Non-contiguous recording sequence')
                    values = (row['sample_count'], row['center_hz'], row['span_hz'], row['requested_rbw_hz'], row['tuned_center_hz'], row['rf_attenuation_db'], row['if_gain_db'], row['first_if_hz'], int(row['data_inverted']))
                    settings.write(' '.join(map(str, values)) + '\n')
                    count += 1
            output = Path(temporary) / 'replay.jsonl'
            with output.open('w') as stream:
                subprocess.run([str(args.binary.resolve()), str(args.tables.resolve()), str(directory / 'samples.if'), str(settings_path)], stdout=stream, check=True, timeout=180)
            maximum = [0.0, 0.0, 0.0]
            with output.open() as stream:
                for row in spectrum_records(directory / 'spectra.jsonl'):
                    actual = json.loads(next(stream))
                    maximum = [max(a, b) for a, b in zip(maximum, compare(row, actual))]
                if stream.read():
                    raise ValueError('Extra replay spectra')
            if not count:
                raise ValueError('Empty recording')
            results.append(dict(recording=run, spectra=count, input_sha256=hashes,
                axis_max_error_hz=maximum[0], enbw_max_error_hz=maximum[1], level_max_error_db=maximum[2]))
    totals = dict(spectra=sum(row['spectra'] for row in results), recordings=len(results),
        axis_max_error_hz=max(row['axis_max_error_hz'] for row in results),
        enbw_max_error_hz=max(row['enbw_max_error_hz'] for row in results),
        level_max_error_db=max(row['level_max_error_db'] for row in results))
    report = dict(schema_version=1, numerical_match=totals['axis_max_error_hz'] <= 1e-6 and totals['enbw_max_error_hz'] <= 1e-9 and totals['level_max_error_db'] <= 1e-9,
        absolute_accuracy_verified=False, hardware_qualification=False,
        reference_manifest_sha256=digest(args.manifest), binary_sha256=digest(args.binary),
        calibration_blocks_sha256={str(i): hashlib.sha256(data).hexdigest() for i, data in tables.items()},
        **totals, runs=results)
    with args.output.open('x') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'runs'}))
    return 0 if report['numerical_match'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
