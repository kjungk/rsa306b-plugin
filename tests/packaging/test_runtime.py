"""Inspect imports from PE bytes without needing a Windows execution host."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('runtime_common', Path(__file__).resolve().parents[2] / 'scripts/runtime-common.py')
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)


def pe(import_name=b'KERNEL32.dll'):
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 64)
    data[64:68] = b'PE\0\0'
    struct.pack_into('<HH', data, 68, 0x8664, 1)
    struct.pack_into('<H', data, 84, 240)
    struct.pack_into('<H', data, 88, 0x20b)
    struct.pack_into('<I', data, 148, 512)  # SizeOfHeaders.
    struct.pack_into('<II', data, 208, 0x1000, 40)  # Import data directory.
    data[328:336] = b'.idata\0\0'
    struct.pack_into('<IIII', data, 336, 512, 0x1000, 512, 512)
    struct.pack_into('<IIIII', data, 512, 0x1080, 0, 0, 0x1060, 0x1080)
    data[608:608+len(import_name)] = import_name
    return data


class PeImportsTests(unittest.TestCase):
    def inspect(self, data):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / 'worker.exe'
            path.write_bytes(data)
            return common.pe_imports(path)

    def test_imports_are_resolved_through_section_virtual_addresses(self):
        self.assertEqual(self.inspect(pe()), ['kernel32.dll'])
        self.assertEqual(self.inspect(pe(b'VCRUNTIME140.dll')), ['vcruntime140.dll'])

    def test_no_imports_is_valid(self):
        data = pe()
        struct.pack_into('<II', data, 208, 0, 0)
        self.assertEqual(self.inspect(data), [])

    def test_truncated_and_unmapped_imports_fail(self):
        cases = [pe()[:350]]
        bad = pe()
        struct.pack_into('<I', bad, 524, 0x9000)
        cases.append(bad)
        bad = pe()
        struct.pack_into('<I', bad, 212, 20)  # Missing terminating descriptor.
        cases.append(bad)
        bad = pe()
        bad[608:864] = b'x' * 256
        cases.append(bad)
        for data in cases:
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                self.inspect(data)


if __name__ == '__main__':
    unittest.main()
