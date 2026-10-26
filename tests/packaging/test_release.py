"""Release safety and immutable qualification tests; no hardware or downloads."""
import importlib.util
import json
from pathlib import Path
import stat
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('verify', ROOT / 'scripts/verify-release.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


def fixture(directory, *, dirty=False):
    arm = b'\xcf\xfa\xed\xfe' + struct.pack('<I', 0x100000c) + bytes(64)
    pe = bytearray(128)
    pe[:2] = b'MZ'
    struct.pack_into('<I', pe, 0x3c, 64)
    pe[64:68] = b'PE\0\0'
    struct.pack_into('<H', pe, 68, 0x8664)
    files = {name: b'fixture' for name in ['main.js', 'adapter.js', 'package-lock.json', 'LICENSE',
        'LICENSE-TEMPLATE', 'THIRD-PARTY-NOTICES.md', 'driver/windows/worker.py',
        'sources/native/CMakeLists.txt', 'sources/scripts/build-native.py', 'sources/release/runtime-pins.json',
        'runtime/darwin-arm64/sources/libusb-1.0.30.tar.bz2', 'runtime/win32-x64/LICENSE.txt']}
    files['soundbase-plugin.json'] = json.dumps({'id': 'rsa306b', 'version': '0.1.0', 'license': 'MIT',
        'platforms': verify.TARGETS, 'runtime': {'type': 'node', 'entrypoint': 'main.js'}}).encode()
    files['package.json'] = json.dumps({'version': '0.1.0', 'soundbase': {'commit': 'a' * 40, 'dirty': dirty}}).encode()
    files['runtime/darwin-arm64/rsa306'] = arm
    files['runtime/darwin-arm64/libusb-1.0.0.dylib'] = arm
    files['runtime/win32-x64/python.exe'] = bytes(pe)
    for target in verify.TARGETS:
        prefix = f'runtime/{target}/'
        entries = {name.removeprefix(prefix): verify.sha(data) for name, data in files.items() if name.startswith(prefix)}
        files[prefix + 'runtime.json'] = json.dumps({'target': target, 'files_sha256': entries}).encode()
    files['SHA256SUMS'] = ''.join(f'{verify.sha(data)}  {name}\n' for name, data in sorted(files.items())).encode()
    archive = directory / 'candidate.zip'
    write_archive(archive, files)
    return archive, files


def write_archive(path, files):
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in files.items():
            info = zipfile.ZipInfo('rsa306b/' + name)
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | (0o755 if name.endswith('/rsa306') else 0o644)) << 16
            archive.writestr(info, data)


class ReleaseTests(unittest.TestCase):
    def test_roundtrip_checks_all_files_and_preserves_worker_mode(self):
        with tempfile.TemporaryDirectory() as work:
            d = Path(work)
            archive, files = fixture(d)
            result = verify.verify_archive(archive, d / 'package with spaces é')
            self.assertEqual(result['files'], len(files))
            self.assertFalse(result['source_dirty'])
            worker = d / 'package with spaces é/rsa306b/runtime/darwin-arm64/rsa306'
            self.assertEqual(worker.read_bytes(), files['runtime/darwin-arm64/rsa306'])
            if __import__('os').name != 'nt':
                self.assertTrue(worker.stat().st_mode & 0o111)
            with self.assertRaisesRegex(ValueError, 'already exists'):
                verify.verify_archive(archive, d / 'package with spaces é')

    def test_tamper_and_unlisted_file_are_refused(self):
        for kind in ['tamper', 'extra']:
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as work:
                d = Path(work)
                archive, files = fixture(d)
                files['adapter.js' if kind == 'tamper' else 'extra.js'] = b'changed'
                write_archive(archive, files)
                with self.assertRaisesRegex(ValueError, 'checksum|Checksum'):
                    verify.verify_archive(archive)

    def test_unsafe_and_windows_alias_paths_are_refused(self):
        for name in ['../x', '/x', 'x/../../y', 'x\\y', 'C:/x', 'x/./y', 'x/NUL.txt', 'x/foo.', 'x/foo ']:
            with self.subTest(name=name), self.assertRaises(ValueError):
                verify.safe_name(name)

    def test_symlink_and_duplicate_entries_are_refused(self):
        for kind in ['symlink', 'duplicate']:
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as work:
                archive, _ = fixture(Path(work))
                with zipfile.ZipFile(archive, 'a') as z:
                    info = zipfile.ZipInfo('rsa306b/EXTRA' if kind == 'symlink' else 'rsa306b/ADAPTER.JS')
                    info.create_system = 3
                    info.external_attr = ((stat.S_IFLNK if kind == 'symlink' else stat.S_IFREG) | 0o644) << 16
                    z.writestr(info, b'target')
                with self.assertRaises(ValueError):
                    verify.verify_archive(archive)

    def test_architectures_are_restricted(self):
        with self.assertRaises(ValueError):
            verify.require_arm64(b'\xcf\xfa\xed\xfe' + struct.pack('<I', 0x1000007))
        with self.assertRaises(ValueError):
            verify.require_x64(b'not an executable')

    def test_pending_dirty_wrong_artifact_and_changed_evidence_block_publication(self):
        with tempfile.TemporaryDirectory() as work:
            d = Path(work)
            archive, _ = fixture(d)
            result = verify.verify_archive(archive)
            q = {key: result[key] for key in ['plugin_id', 'version', 'source_commit', 'artifact_sha256']}
            q.update(schema_version=1, checks={})
            path = d / 'qualification.json'
            path.write_text(json.dumps(q))
            with self.assertRaisesRegex(ValueError, 'has not passed'):
                verify.verify_qualification(result, path)
            for name in ['macos_hardware', 'windows_hardware', 'signal_generator']:
                report = d / (name + '.json')
                report.write_text(json.dumps({'status': 'passed', 'artifact_sha256': result['artifact_sha256']}))
                q['checks'][name] = {'status': 'passed', 'report': report.name, 'sha256': verify.sha(report.read_bytes())}
            path.write_text(json.dumps(q))
            verify.verify_qualification(result, path)
            with self.assertRaisesRegex(ValueError, 'committed source'):
                verify.verify_qualification({**result, 'source_dirty': True}, path)
            with self.assertRaisesRegex(ValueError, 'artifact_sha256'):
                verify.verify_qualification({**result, 'artifact_sha256': 'f' * 64}, path)
            (d / 'signal_generator.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'missing or changed'):
                verify.verify_qualification(result, path)


if __name__ == '__main__':
    unittest.main()
