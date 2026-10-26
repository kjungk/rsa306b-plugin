import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("RSA306_BINARY"), "Run via CTest to supply the CLI binary")
class CliTests(unittest.TestCase):
    def call(self, *args):
        return subprocess.run([os.environ["RSA306_BINARY"], *map(str, args)], capture_output=True,
                              text=True, timeout=10)

    def test_native_gate_and_arguments(self):
        settings = ["--center-hz", "600e6", "--span-hz", "20e6", "--rbw-hz", "10e3", "--reference-dbm", "-20"]
        for command, extra in (("capture", []), ("monitor", ["--count", "3"])):
            result = self.call(command, *settings, *extra)
            self.assertEqual(result.returncode, 3, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertEqual(json.loads(result.stderr)["code"], "native_reference_uncalibrated")
        for args in (("capture",), ("discover", "--unknown", "x"), ("read-calibration", "--block", "7"), ("unknown",),
                     ("capture", *settings, "--center-hz", "500e6"),
                     ("monitor", *settings, "--count", "-1")):
            self.assertEqual(self.call(*args).returncode, 2)

    def test_capabilities_and_calibration_gate(self):
        capabilities = json.loads(self.call("capabilities").stdout)
        self.assertEqual(capabilities["level_units"], ["dBFS", "dBm"])
        self.assertTrue(capabilities["calibrated"])
        self.assertFalse(capabilities["absolute_accuracy_verified"])
        self.assertEqual(capabilities["calibrated_min_hz"], 9000)
        self.assertEqual(capabilities["hardware_max_hz"], 6.2e9)
        self.assertEqual(capabilities["max_frequency_hz"], 6.2e9)
        self.assertEqual(capabilities["min_sweep_hz"], 9000)
        self.assertEqual(capabilities["lf_limit_hz"], 22e6)
        for args in (
            ["capture", "--center-hz", "594e6", "--span-hz", "1e6", "--rbw-hz", "1e4", "--attenuation-db", "31.5"],
        ):
            result = self.call(*args, "--level-unit", "dbm")
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
            self.assertEqual(json.loads(result.stderr)["code"], "unsupported_calibration")
        plan = self.call("plan-sweep", "--start-hz", "22e6", "--stop-hz", "6.2e9", "--rbw-hz", "1e4", "--level-unit", "dbm")
        self.assertEqual(plan.returncode, 0, plan.stderr)

    def test_sweep_planning_and_preflight_without_usb(self):
        args = ["--start-hz", "470e6", "--stop-hz", "694e6", "--rbw-hz", "10e3"]
        result = self.call("plan-sweep", *args)
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertEqual(plan["type"], "sweep_plan")
        self.assertEqual(len(plan["segments"]), 13)
        self.assertEqual(plan["segments"][-1]["center_hz"], 684e6)
        for extra in (["--count", "0"], ["--count", "1", "--interval-ms", "-1"],
                      ["--count", "1", "--duration-seconds", "0"],
                      ["--count", "1", "--duration-seconds", "86401"],
                      ["--count", "1", "--if-gain-db", "99"]):
            invalid = self.call("sweep", *args, *extra)
            self.assertEqual(invalid.returncode, 2, invalid.stderr)
            self.assertEqual(invalid.stdout, "")
        huge = self.call("plan-sweep", "--start-hz", "11e6", "--stop-hz", "1500e6", "--rbw-hz", "161")
        self.assertEqual(huge.returncode, 2)
        self.assertEqual(json.loads(huge.stderr)["code"], "sweep_too_large")

    def test_native_preflight_never_needs_hardware(self):
        base = ["capture", "--center-hz", "600e6", "--span-hz", "20e6", "--rbw-hz", "10e3"]
        for option, value in [("--attenuation-db", "0.25"), ("--attenuation-db", "32"),
                              ("--if-gain-db", "-12"), ("--if-gain-db", "20.5")]:
            result = self.call(*base, option, value)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertEqual(result.stdout, "")
        bad_band = self.call("capture", "--center-hz", "600123456", "--span-hz", "40e6", "--rbw-hz", "10e3")
        self.assertEqual(bad_band.returncode, 2, bad_band.stderr)

    def test_portable_decimal_cli_grammar(self):
        args = ['plan-sweep', '--start-hz', '470e6', '--stop-hz', '694e6', '--rbw-hz', '1.025390625e+4']
        for value in ('-1.5', '0', '-0', '1.1e1', '.5', '1.', '0e-9999'):
            result = self.call(*args, '--if-gain-db', value)
            self.assertEqual(result.returncode, 0, result.stderr)
        for value in ('', ' 1', '1 ', '+1', '1,5', '1e', '0x1p2', 'nan', 'inf', '1e9999', '1e-9999'):
            result = self.call(*args, '--if-gain-db', value)
            self.assertEqual(result.returncode, 2, (value, result.stderr))
            self.assertEqual(json.loads(result.stderr)['code'], 'invalid_arguments')

    def test_real_if_input_and_unknown_metadata(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "adc.if"
            n, tone = 32768, 7000
            path.write_bytes(b"".join(struct.pack("<h", round(16384 * math.cos(2*math.pi*tone*i/n))) for i in range(n)))
            args = ["analyze-if", "--input", path, "--center-hz", "610e6", "--span-hz", "20e6", "--rbw-hz", "10e3"]
            result = self.call(*args)
            self.assertEqual(result.returncode, 0, result.stderr)
            record = json.loads(result.stdout)
            peak = max(range(len(record["levels"])), key=record["levels"].__getitem__)
            self.assertEqual(record["frequency_hz"][peak], 610e6 - 28e6 + 112e6*tone/n)
            self.assertAlmostEqual(record["levels"][peak], -6.0206, places=3)
            self.assertEqual(record["source"], "offline-if-samples")
            for field in ["device_timestamp_ticks", "acquisition_start_unix_seconds", "rf_attenuation_db", "if_gain_db"]:
                self.assertIsNone(record[field])
            path.write_bytes(path.read_bytes()[:-1])
            self.assertEqual(self.call(*args).returncode, 5)

    def test_iq_frames_and_time(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "normalized.iq"
            n = 1024
            frame = b"".join(struct.pack("<ff", 0.5 * math.cos(2 * math.pi * 123 * i / n),
                                        0.5 * math.sin(2 * math.pi * 123 * i / n)) for i in range(n))
            path.write_bytes(frame * 2)
            args = ["analyze-iq", "--input", path, "--center-hz", "600e6", "--sample-rate-hz", "1024000",
                    "--fft-size", n, "--count", 2]
            result = self.call(*args, "--start-unix-seconds", "1000")
            self.assertEqual(result.returncode, 0, result.stderr)
            records = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual(len(records), 2)
            for record in records:
                self.assertEqual(record["source"], "offline-iq")
                self.assertFalse(record["calibrated"])
                self.assertEqual(record["level_unit"], "dBFS")
                self.assertEqual(record["quality"]["gap_status"], "unknown")
                peak = max(range(n), key=record["levels"].__getitem__)
                self.assertEqual(record["frequency_hz"][peak], 600123000)
                self.assertAlmostEqual(record["levels"][peak], -6.020599913, places=5)
            self.assertAlmostEqual(records[1]["acquisition_start_unix_seconds"], 1000.001)
            self.assertEqual(records[1]["sample_offset"], n)
            unknown = self.call(*args)
            self.assertIsNone(json.loads(unknown.stdout.splitlines()[0])["acquisition_start_unix_seconds"])
            path.write_bytes(frame + b"\0\0\0")
            partial = self.call(*args)
            self.assertEqual(partial.returncode, 5)
            self.assertEqual(len(partial.stdout.splitlines()), 1)
            self.assertEqual(json.loads(partial.stderr)["code"], "incomplete_frame")

    def test_reject_nonfinite_and_invalid_formats(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.iq"
            args = ["analyze-iq", "--input", path, "--center-hz", "600e6", "--sample-rate-hz", "1e6", "--fft-size", "8"]
            for value in (float("nan"), float("inf"), 2.0):
                path.write_bytes(struct.pack("<ff", value, 0) * 8)
                result = self.call(*args)
                self.assertEqual(result.returncode, 5)
                self.assertEqual(result.stdout, "")
            self.assertEqual(self.call("capture", "--center-hz", "nan", "--span-hz", "1e6",
                                       "--rbw-hz", "1000", "--reference-dbm", "0").returncode, 2)


if __name__ == "__main__":
    unittest.main()
