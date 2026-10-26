#!/usr/bin/env python3
"""Compare stored-table calculations with paired Windows USB/IF API references.

Offline only. A successful comparison is numerical evidence, not permission to
label native measurements calibrated. No vendor library is loaded or executed.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
from calibration_math import FactoryModel
from decode_usb import decode
from inspect_calibration import BlockCollector, describe


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


class Commands(BlockCollector):
    def __init__(self):
        super().__init__()
        self.registers, self.spi, self.pll, self.states = {}, {}, {}, {}

    def write(self, line):
        super().write(line)
        c = json.loads(line)
        if c['response'].get('status') or c['response'].get('auxiliary'):
            raise ValueError('Failed captured device command')
        for pair in c['request'].get('registers', []):
            a, v = pair['address'], pair['value']
            base = a & 0x3fff
            if a & 0xc000 == 0xc000:
                raise ValueError('Invalid register alias')
            if a & 0x4000:
                self.registers[base] = self.registers.get(base, 0) & ~v
            elif a & 0x8000:
                self.registers[base] = self.registers.get(base, 0) | v
            else:
                self.registers[base] = v
            if a == 0x20:
                value = self.registers[0x22] | self.registers[0x23] << 16
                if v == 4:
                    self.pll[value & 7] = value
                else:
                    self.spi[v] = value
            if a == 0x43 and v != 65535:
                self.states[v] = (self.registers.copy(), self.spi.copy(), self.pll.copy())


class Frames:
    def __init__(self):
        self.tags = {}

    def write(self, line):
        f = json.loads(line)
        tag, tick = f['descriptor'], int(f['timestamp_ticks'])
        if tag == 65535:
            return
        row = self.tags.setdefault(tag, dict(first_tick=tick, last_tick=tick, frames=0, status=0, gaps=0))
        if row['frames'] and not f['contiguous_with_previous']:
            row['gaps'] += 1
        row.update(last_tick=tick, frames=row['frames']+1, status=row['status'] | f['status'])


def check_capture(directory):
    manifest_path = directory/'capture.json'
    manifest = json.loads(manifest_path.read_text())
    if manifest['status'] != 'complete' or manifest['api_exit_code'] != 0:
        raise ValueError('Incomplete reference capture')
    if not {'usb.pcap', 'api/spectra.jsonl', 'api/manifest.json'} <= manifest['files_sha256'].keys():
        raise ValueError('Missing reference hashes')
    for name, expected in manifest['files_sha256'].items():
        file = (directory/name).resolve()
        if not file.is_relative_to(directory.resolve()) or digest(file) != expected:
            raise ValueError(f'Reference hash/path mismatch: {name}')
    commands, frames = Commands(), Frames()
    decoded = decode(directory/'usb.pcap', device=int(manifest['devices']), command_sink=commands, frame_sink=frames)
    blocks = list(commands.payloads())
    tables = {i: describe(i, data) for i, _, data in blocks if i in (2, 3, 4, 5, 6)}
    model = FactoryModel(tables)
    block_hashes = {str(i): hashlib.sha256(data).hexdigest() for i, _, data in blocks}
    records = []
    with (directory/'api/spectra.jsonl').open() as stream:
        for line in iter(lambda: stream.readline(8*1024*1024+1), ''):
            if len(line) > 8*1024*1024 or len(records) >= 1000:
                raise ValueError('Oversized spectrum reference')
            s = json.loads(line)
            settings, reference = s['actual_settings'], s['if_scaling_reference']
            if s['quality']['raw_status'] != 0:
                raise ValueError('Flagged Windows spectrum')
            tick = int(s['device_timestamp_ticks'])
            matched = []
            for tag, f in frames.tags.items():
                reg, spi, pll = commands.states[tag]
                lf = bool(reg[0x32] & 1)
                # Observed pipeline delay: API trace timestamp precedes its
                # first raw USB sample by 9 ticks (LF) or 68 ticks (RF).
                if f['first_tick']-tick == (9 if lf else 68):
                    matched.append((tag, f, reg, spi, pll, lf))
            if len(matched) != 1:
                raise ValueError('Spectrum timestamp cannot be paired to one USB tag')
            tag, frame, reg, spi, pll, lf = matched[0]
            if frame['status'] or frame['gaps']:
                raise ValueError('Flagged or discontinuous paired raw samples')
            center = settings['center_hz']
            signed_atten = spi[2]-(1 << 32) if spi[2] >= 1 << 31 else spi[2]
            atten, gain = (-signed_atten-1)/2, (40-spi[16])/2
            factor = model.scale(center, reg[0x30], atten, gain, spi[1] if lf else None)
            scale_error = 20*math.log10(factor/reference['volts_per_adc_count'])
            eq_axis, expected_eq = reference['eq_frequency_hz'], reference['eq_amplitude_db']
            if lf:
                tuned = center
                predicted = model.lf_eq(center, spi[1], eq_axis)
            else:
                first_if = 2440e6 if reg[0x34] & 4 else 1190e6
                high = bool(reg[0x40] & 1)
                lo = ((pll[0] >> 15) + ((pll[0] >> 3) & 4095)/((pll[1] >> 3) & 4095))*28e6/(1 << ((pll[4] >> 20) & 7))
                tuned = lo-first_if if high else lo+first_if
                predicted = model.rf_eq(center, tuned, reg[0x30], first_if, high, eq_axis)
            if len(predicted) != len(expected_eq) or not all(math.isfinite(x) for x in expected_eq):
                raise ValueError('Malformed correction reference')
            max_error = max(abs(a-b) for a, b in zip(predicted, expected_eq))
            records.append(dict(phase=s['phase'], descriptor=tag, api_timestamp_ticks=str(tick),
                first_usb_timestamp_ticks=str(frame['first_tick']), raw_frames=frame['frames'],
                requested_center_hz=center, tuned_center_hz=tuned, lf_path=lf,
                rf_register=reg[0x30], attenuation_db=atten, if_gain_db=gain, lf_code=spi.get(1) if lf else None,
                volts_per_adc_count=reference['volts_per_adc_count'], predicted_volts_per_adc_count=factor,
                scale_error_db=scale_error, eq_points=len(predicted), eq_max_error_db=max_error))
    if not records:
        raise ValueError('No scaling references')
    return dict(run=str(directory), manifest_sha256=digest(manifest_path),
                pcap_sha256=manifest['files_sha256']['usb.pcap'], calibration_blocks_sha256=block_hashes,
                raw_if_frames=decoded['if_frames'], records=records)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('captures', nargs='+', type=Path, help='Capture directories or completed batch directories')
    p.add_argument('--output', required=True, type=Path, help='New JSON report outside Git')
    a = p.parse_args()
    if a.output.exists():
        p.error('Output already exists')
    directories = []
    for directory in a.captures:
        if (directory/'capture.json').is_file():
            directories.append(directory)
        else:
            batch = json.loads((directory/'batch.json').read_text())
            if batch['status'] != 'complete':
                raise ValueError('Incomplete batch')
            found = sorted(x.parent for x in directory.glob('*/capture.json'))
            if len(found) != len(batch['plan']):
                raise ValueError('Incomplete batch download')
            directories.extend(found)
    if not 1 <= len(directories) <= 512 or len(set(p.resolve() for p in directories)) != len(directories):
        raise ValueError('Duplicate or excessive captures')
    runs = [check_capture(d) for d in directories]
    if any(r['calibration_blocks_sha256'] != runs[0]['calibration_blocks_sha256'] for r in runs):
        raise ValueError('Stored calibration changed between reference captures')
    records = [s for r in runs for s in r['records']]
    scale_error = max(abs(s['scale_error_db']) for s in records)
    eq_error = max(s['eq_max_error_db'] for s in records)
    # LF interpolation normalization retains a small, explicitly reported
    # residual. This tolerance does not certify RF amplitude accuracy.
    passed = scale_error <= 0.00001 and eq_error <= 0.0002
    report = dict(schema_version=1, basis='offline numerical comparison; not RF accuracy certification',
                  calibrated=False, numerical_match=passed, capture_count=len(runs), spectrum_count=len(records),
                  scale_max_error_db=scale_error, eq_max_error_db=eq_error,
                  scale_tolerance_db=0.00001, eq_tolerance_db=0.0002, runs=runs)
    with a.output.open('x') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'runs'}))
    return 0 if passed else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, IndexError, ZeroDivisionError) as error:
        print(f'Calibration reference check failed: {error}', file=sys.stderr)
        raise SystemExit(1)
