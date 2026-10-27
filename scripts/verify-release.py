#!/usr/bin/env python3
"""Inspect/extract a candidate, or gate publication against qualification evidence.

This does not execute code, install the plugin, or open USB.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import zipfile

MAX_ARCHIVE = 250 * 1024 * 1024
MAX_EXPANDED = 750 * 1024 * 1024
TARGETS = ['darwin-arm64', 'win32-x64']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe_name(name):
    p = PurePosixPath(name)
    if not name or p.is_absolute() or '..' in p.parts or '\\' in name or ':' in name:
        raise ValueError(f'Unsafe archive path: {name}')
    reserved = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)), *(f'LPT{i}' for i in range(1, 10))}
    if str(p) != name or any(part.endswith(('.', ' ')) or part.split('.')[0].upper() in reserved for part in p.parts):
        raise ValueError(f'Non-portable archive path: {name}')
    return p


def require_x64(data):
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Windows runtime binary is not a PE executable')
    pos = struct.unpack_from('<I', data, 0x3c)[0]
    if pos + 6 > len(data) or data[pos:pos + 4] != b'PE\0\0' or struct.unpack_from('<H', data, pos + 4)[0] != 0x8664:
        raise ValueError('Windows runtime binary is not Windows x64')


def require_arm64(data):
    if len(data) < 8 or data[:4] != b'\xcf\xfa\xed\xfe' or struct.unpack_from('<I', data, 4)[0] != 0x100000c:
        raise ValueError('Native worker/library must be thin macOS arm64 Mach-O')


def verify_archive(path, extract=None):
    path = Path(path)
    if path.stat().st_size > MAX_ARCHIVE:
        raise ValueError('Lab package exceeds 250 MiB')
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) > 50000 or sum(e.file_size for e in entries) > MAX_EXPANDED:
            raise ValueError('Excessive expanded package')
        names, seen = [], set()
        for entry in entries:
            p = safe_name(entry.filename)
            kind = stat.S_IFMT(entry.external_attr >> 16)
            if entry.is_dir() or kind not in (0, stat.S_IFREG):
                raise ValueError('Package must contain regular files, without symlinks')
            if entry.filename.casefold() in seen:
                raise ValueError('Duplicate or case-colliding archive entry')
            seen.add(entry.filename.casefold())
            names.append(str(p))
        roots = {name.split('/')[0] for name in names}
        if roots != {'rsa306b'} or any('/' not in name for name in names):
            raise ValueError('Expected exactly one rsa306b/ package folder')
        payload = {name.removeprefix('rsa306b/'): archive.read(name) for name in names}
        required = ['soundbase-plugin.json', 'package.json', 'package-lock.json', 'main.js', 'adapter.js',
                    'LICENSE', 'LICENSE-SOUNDBASE', 'THIRD-PARTY-NOTICES.md', 'SHA256SUMS',
                    'runtime/darwin-arm64/runtime.json', 'runtime/win32-x64/runtime.json',
                    'runtime/darwin-arm64/rsa306', 'runtime/darwin-arm64/libusb-1.0.0.dylib',
                    'runtime/darwin-arm64/sources/libusb-1.0.30.tar.bz2',
                    'runtime/win32-x64/python.exe', 'runtime/win32-x64/rsa306.exe', 'runtime/win32-x64/LICENSE.txt',
                    'driver/windows/worker.py', 'sources/native/CMakeLists.txt',
                    'sources/scripts/build-native.py', 'sources/scripts/build-native-windows.py',
                    'sources/scripts/assemble-runtime.py', 'sources/release/runtime-pins.json']
        for name in required:
            if name not in payload:
                raise ValueError(f'Missing release file: {name}')
        manifest = json.loads(payload['soundbase-plugin.json'])
        package = json.loads(payload['package.json'])
        if manifest['id'] != 'rsa306b' or manifest['version'] != package['version']:
            raise ValueError('Plugin identity/version mismatch')
        if sorted(manifest.get('platforms', [])) != TARGETS:
            raise ValueError('Only macOS arm64 and Windows x64 may be declared')
        if not manifest.get('license') or manifest['runtime'] != {'type': 'node', 'entrypoint': 'main.js'}:
            raise ValueError('Missing license or unexpected entrypoint')
        checksums = {}
        for line in payload['SHA256SUMS'].decode().splitlines():
            match = re.fullmatch(r'([a-f0-9]{64})  (.+)', line)
            if not match or match[2] in checksums:
                raise ValueError('Invalid checksum manifest')
            checksums[match[2]] = match[1]
        if set(checksums) != set(payload) - {'SHA256SUMS'}:
            raise ValueError('Checksum manifest does not cover exactly the package files')
        for name, expected in checksums.items():
            if sha(payload[name]) != expected:
                raise ValueError(f'Package checksum mismatch: {name}')
        for target in TARGETS:
            prefix = f'runtime/{target}/'
            runtime = json.loads(payload[prefix + 'runtime.json'])
            if runtime.get('target') != target:
                raise ValueError('Runtime target mismatch')
            actual = {n.removeprefix(prefix) for n in payload if n.startswith(prefix)} - {'runtime.json'}
            if actual != set(runtime['files_sha256']):
                raise ValueError(f'Runtime manifest coverage mismatch: {target}')
            for name, expected in runtime['files_sha256'].items():
                safe_name(name)
                if sha(payload[prefix + name]) != expected:
                    raise ValueError(f'Runtime checksum mismatch: {target}/{name}')
        require_arm64(payload['runtime/darwin-arm64/rsa306'])
        require_arm64(payload['runtime/darwin-arm64/libusb-1.0.0.dylib'])
        require_x64(payload['runtime/win32-x64/python.exe'])
        require_x64(payload['runtime/win32-x64/rsa306.exe'])
        if not next(e for e in entries if e.filename == 'rsa306b/runtime/darwin-arm64/rsa306').external_attr >> 16 & 0o111:
            raise ValueError('Native worker has lost its executable permission')
        if extract is not None:
            extract = Path(extract)
            if extract.exists():
                raise ValueError(f'Extraction destination already exists: {extract}')
            extract.mkdir(parents=True)
            for entry in entries:
                target = extract.joinpath(*PurePosixPath(entry.filename).parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(entry))
                target.chmod((entry.external_attr >> 16) & 0o777 or 0o644)
    return {'schema_version': 1, 'plugin_id': manifest['id'], 'version': manifest['version'],
            'artifact_sha256': sha(path.read_bytes()), 'source_commit': package['soundbase'].get('commit'),
            'source_dirty': package['soundbase'].get('dirty', True),
            'files': len(payload), 'bytes': path.stat().st_size, 'status': 'passed'}


def verify_qualification(result, qualification_path):
    path = Path(qualification_path)
    q = json.loads(path.read_text())
    if q.get('schema_version') != 1:
        raise ValueError('Unknown qualification schema')
    if result.get('source_dirty', True):
        raise ValueError('Publication requires a candidate built from committed source')
    for key in ['plugin_id', 'version', 'artifact_sha256', 'source_commit']:
        if not q.get(key) or q[key] != result[key]:
            raise ValueError(f'Qualification does not match candidate {key}')
    if not re.fullmatch(r'[a-f0-9]{40}', q['source_commit']):
        raise ValueError('Qualification requires a real source commit')
    for name in ['macos_hardware', 'windows_hardware', 'signal_generator']:
        check = q.get('checks', {}).get(name, {})
        if check.get('status') != 'passed':
            raise ValueError(f'Publication blocked: {name} has not passed')
        report_name = check.get('report')
        if not isinstance(report_name, str):
            raise ValueError(f'Missing qualification report: {name}')
        report = path.parent.joinpath(*safe_name(report_name).parts)
        if not report.resolve().is_relative_to(path.parent.resolve()) or report.is_symlink() or not report.is_file() or sha(report.read_bytes()) != check.get('sha256'):
            raise ValueError(f'Qualification evidence is missing or changed: {name}')
        data = json.loads(report.read_text())
        if data.get('status') != 'passed' or data.get('artifact_sha256') != result['artifact_sha256']:
            raise ValueError(f'Evidence is not for the qualified artifact: {name}')
    return q


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--extract', type=Path)
    parser.add_argument('--qualification', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = verify_archive(args.archive, args.extract)
    if args.qualification:
        verify_qualification(result, args.qualification)
        result['qualification'] = 'passed'
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
