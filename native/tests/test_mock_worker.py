"""The actual native executable and DSP pipeline, with no physical USB access."""
import json
import math
import os
import subprocess
import unittest


def decode(line):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate JSON member: ' + key)
            result[key] = value
        return result
    return json.loads(line, object_pairs_hook=unique)


@unittest.skipUnless(os.environ.get('RSA306_BINARY'), 'Run via CTest')
class MockWorkerTests(unittest.TestCase):
    def command(self, command, *args):
        return [os.environ['RSA306_BINARY'], command, '--mock', *map(str, args)]

    def sweep(self, start=21000000, stop=23000000, count=1):
        return self.command('sweep', '--start-hz', start, '--stop-hz', stop,
                            '--rbw-hz', 10000, '--level-unit', 'dbm', '--count', count,
                            '--interval-ms', 0, '--serial', 'MOCK-RSA306B')

    def test_generated_lf_rf_pipeline_and_serial_guard(self):
        result = subprocess.run(self.sweep(), capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [decode(line) for line in result.stdout.splitlines()]
        self.assertEqual(rows[0]['serial'], 'MOCK-RSA306B')
        self.assertTrue(rows[0]['synthetic'])
        spectra = [row for row in rows if row['type'] == 'spectrum']
        self.assertEqual([s['receiver_path'] for s in spectra], ['direct-lf', 'heterodyne-rf'])
        self.assertEqual(len({s['calibration']['id'] for s in spectra}), 1)
        for row in spectra:
            self.assertEqual(row['source'], 'synthetic-mock')
            self.assertTrue(row['synthetic'])
            self.assertTrue(row['calibration']['synthetic'])
            self.assertEqual(row['calibration']['algorithm'], 'hw7-fullrange-v1')
            self.assertEqual(row['level_unit'], 'dBm')
            self.assertTrue(all(map(math.isfinite, row['levels'])))
            self.assertEqual(len(row['frequency_hz']), len(row['levels']))
            self.assertEqual(row['quality']['raw_status'], 0)
            self.assertFalse(row['calibration']['absolute_accuracy_verified'])
        self.assertTrue(rows[-1]['complete'])
        wrong = subprocess.run(self.command('probe', '--serial', 'WRONG'), capture_output=True, text=True, timeout=5)
        self.assertEqual(wrong.returncode, 4)
        self.assertEqual(decode(wrong.stderr)['code'], 'wrong_device')

    def test_full_range_plan_keeps_lf_rf_domains_separate(self):
        result = subprocess.run(self.command('plan-sweep', '--start-hz', 9000, '--stop-hz', 6.2e9,
                                            '--rbw-hz', 10000, '--level-unit', 'dbm'), capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        segments = decode(result.stdout)['segments']
        self.assertLessEqual(len(segments), 512)
        self.assertEqual(segments[0]['center_hz'] - segments[0]['span_hz']/2, 9000)
        self.assertEqual(segments[-1]['center_hz'] + segments[-1]['span_hz']/2, 6.2e9)
        for segment in segments:
            center, half = segment['center_hz'], segment['span_hz']/2
            if center < 22e6:
                self.assertLessEqual(center + half, 22e6)
            else:
                self.assertGreaterEqual(center - half, 22e6)

    def test_tiny_boundary_fragments_expand_inside_their_own_domain(self):
        for start,stop in ((21999999,22100000),(21900000,22000001)):
            with self.subTest(start=start,stop=stop):
                result=subprocess.run(self.sweep(start,stop),capture_output=True,text=True,timeout=10)
                self.assertEqual(result.returncode,0,result.stderr)
                rows=[decode(line) for line in result.stdout.splitlines()]
                begin=next(row for row in rows if row['type']=='sweep_begin')
                self.assertEqual((begin['start_hz'],begin['stop_hz']),(start,stop))
                spectra=[row for row in rows if row['type']=='spectrum']
                self.assertEqual(len(spectra),2)
                for row in spectra:
                    self.assertGreaterEqual(len(row['frequency_hz']),2)
                    if row['receiver_path']=='direct-lf':
                        self.assertLessEqual(max(row['frequency_hz']),22e6)
                    else:
                        self.assertGreaterEqual(min(row['frequency_hz']),22e6)
                self.assertTrue(rows[-1]['complete'])

    def test_stop_and_parent_eof_wait_for_cleanup(self):
        for control in ('stop\n', None):
            with self.subTest(control=control):
                process = subprocess.Popen(self.sweep(count=1000000)+['--stdin-control'],
                                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    while decode(process.stdout.readline())['type'] != 'sweep_end':
                        pass
                    if control is not None:
                        process.stdin.write(control)
                        process.stdin.flush()
                    process.stdin.close()
                    process.stdin = None
                    output, error = process.communicate(timeout=5)
                    self.assertEqual(process.returncode, 0, error)
                    self.assertEqual(decode(output.splitlines()[-1]), {'type': 'stopped', 'cleanup_complete': True})
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
