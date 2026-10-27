"""Inspect imports from PE bytes without needing a Windows execution host."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('runtime_common', Path(__file__).resolve().parents[2] / 'scripts/runtime-common.py')
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)
build_spec = importlib.util.spec_from_file_location('windows_build', Path(__file__).resolve().parents[2] / 'scripts/build-native-windows.py')
windows_build = importlib.util.module_from_spec(build_spec)
build_spec.loader.exec_module(windows_build)


class VisualStudioSelectionTests(unittest.TestCase):
    generators = [{'name': 'Visual Studio 17 2022'}, {'name': 'Visual Studio 18 2026'}, {'name': 'Ninja'}]

    @staticmethod
    def instance(version, **kwargs):
        return {'installationVersion': version, 'installationPath': f'C:/Build tools/{version}',
                'isComplete': True, 'isLaunchable': True, **kwargs}

    def test_runner_with_only_new_visual_studio_does_not_request_old_year(self):
        installed = self.instance('18.0.100.1')
        generator, instance = windows_build.select_visual_studio([installed], self.generators)
        self.assertEqual(generator, 'Visual Studio 18 2026')
        self.assertEqual(instance['installationPath'], installed['installationPath'])

    def test_newest_compatible_installation_and_explicit_override(self):
        older, newer = self.instance('17.14.100.1'), self.instance('18.2.100.1')
        unrecognized = self.instance('19.0.100.1')
        incomplete = self.instance('18.3.100.1', isComplete=False)
        instances = [older, newer, unrecognized, incomplete]
        self.assertEqual(windows_build.select_visual_studio(instances, self.generators)[1], newer)
        self.assertEqual(windows_build.select_visual_studio(instances, self.generators, 'Visual Studio 17 2022')[1], older)

    def test_new_generator_year_comes_from_cmake_capabilities(self):
        generators = self.generators + [{'name': 'Visual Studio 19 2030'}]
        self.assertEqual(windows_build.select_visual_studio([self.instance('19.0.1.1')], generators)[0], 'Visual Studio 19 2030')

    def test_missing_or_incompatible_toolchain_reports_actionable_failure(self):
        for instances, generators, requested in [
            ([], self.generators, None),
            ([self.instance('18.0.1.1')], self.generators[:1], None),
            ([self.instance('18.0.1.1')], self.generators, 'Visual Studio 17 2022'),
            ([self.instance('18.0.1.1', isLaunchable=False)], self.generators, None),
        ]:
            with self.subTest(requested=requested, instances=instances), self.assertRaisesRegex(ValueError, 'Install the x64 C\\+\\+ tools'):
                windows_build.select_visual_studio(instances, generators, requested)
        with self.assertRaisesRegex(ValueError, 'does not advertise'):
            windows_build.select_visual_studio([], self.generators, 'Ninja')


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
