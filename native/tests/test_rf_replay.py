"""Offline replay regression guards; no device or private recording needed."""
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_rf_replay import compare
from test_native_calibration import tables


class ReplayComparisonTests(unittest.TestCase):
    def test_detects_axis_and_level_changes(self):
        reference = dict(frequency_hz=[100.0, 102.0], levels=[-30.0, -40.0], window_enbw_hz=3.0)
        actual = dict(start_hz=100.0, step_hz=2.0, enbw_hz=3.0, levels=[-30.0, -40.0])
        self.assertEqual(compare(reference, actual), (0, 0, 0))
        actual.update(start_hz=101.0, levels=[-31.0, -40.0])
        self.assertEqual(compare(reference, actual), (1, 0, 1))

    def test_rejects_missing_or_nonfinite_points(self):
        reference = dict(frequency_hz=[100.0, 102.0], levels=[-30.0, -40.0], window_enbw_hz=3.0)
        for levels in ([0.0], [math.nan, 0.0]):
            with self.assertRaises(ValueError):
                compare(reference, dict(start_hz=100.0, step_hz=2.0, enbw_hz=3.0, levels=levels))


@unittest.skipUnless(os.environ.get('RSA_RF_REPLAY'), 'Run via CTest')
class ReplayBinaryTests(unittest.TestCase):
    settings = '1024 600000000 20000000 100000 600000000 1 11 2440000000 1\n'

    def call(self, root, raw, settings):
        (root / 'samples.if').write_bytes(raw)
        (root / 'settings.txt').write_text(settings)
        return subprocess.run([os.environ['RSA_RF_REPLAY'], str(root), str(root / 'samples.if'), str(root / 'settings.txt')], capture_output=True, text=True, timeout=10)

    def test_zero_signal_and_refuse_partial_or_extra_blocks(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            tables(root)
            result = self.call(root, bytes(2048), self.settings)
            self.assertEqual(result.returncode, 0, result.stderr)
            spectrum = json.loads(result.stdout)
            self.assertTrue(spectrum['levels'])
            self.assertTrue(all(math.isfinite(value) for value in spectrum['levels']))
            for raw in (bytes(2047), bytes(2049)):
                result = self.call(root, raw, self.settings)
                self.assertNotEqual(result.returncode, 0)
            for settings in ('', self.settings.replace('1024', '1023'), self.settings + 'malformed\n'):
                result = self.call(root, bytes(2048), settings)
                self.assertNotEqual(result.returncode, 0)
