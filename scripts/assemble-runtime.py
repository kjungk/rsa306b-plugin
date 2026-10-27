#!/usr/bin/env python3
"""Assemble a native Windows x64 worker plus pinned fallback Python.

Example: python3 scripts/assemble-runtime.py --native-worker build/rsa306.exe
No Python packages, system drivers, vendor DLLs or firmware are installed.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import stat
import tempfile
import zipfile

spec = importlib.util.spec_from_file_location('runtime_common', Path(__file__).with_name('runtime-common.py'))
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--python-zip', type=Path)
    parser.add_argument('--native-worker', type=Path, required=True)
    parser.add_argument('--native-metadata', type=Path)
    parser.add_argument('--output', type=Path, default=common.ROOT / 'runtime/win32-x64')
    parser.add_argument('--replace', action='store_true')
    args = parser.parse_args()
    worker = args.native_worker.resolve()
    dependencies = common.pe_imports(worker)
    # The direct worker uses only Windows inbox libraries. In particular it
    # must not silently gain a vendor API or compiler-runtime installation need.
    allowed = {'kernel32.dll', 'setupapi.dll', 'advapi32.dll', 'user32.dll',
               'ntdll.dll', 'msvcrt.dll', 'ucrtbase.dll', 'shell32.dll', 'ole32.dll'}
    if any(name not in allowed and not name.startswith('api-ms-win-') for name in dependencies):
        raise ValueError(f'Native worker imports non-inbox DLLs: {dependencies}')
    native_metadata = json.loads(args.native_metadata.read_text()) if args.native_metadata else {}
    if not isinstance(native_metadata, dict):
        raise ValueError('Native metadata must be a JSON object')
    if native_metadata.get('sha256', common.digest(worker)) != common.digest(worker):
        raise ValueError('Native metadata describes a different worker')
    p = common.pins('win32-x64')
    source = common.source_file(args.python_zip, p['python_url'], p['python_sha256'])
    build = common.ROOT / 'build'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='windows-runtime-', dir=build) as work:
        stage = Path(work) / 'win32-x64'
        stage.mkdir()
        with zipfile.ZipFile(source) as archive:
            seen = set()
            for entry in archive.infolist():
                name = common.safe_member(entry.filename)
                mode = entry.external_attr >> 16
                if stat.S_ISLNK(mode) or entry.is_dir() or str(name).casefold() in seen:
                    raise ValueError('Unexpected entry in pinned Python archive')
                seen.add(str(name).casefold())
                target = stage.joinpath(*name.parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(entry))
                target.chmod(0o644)
        for required in ['python.exe', 'python314.dll', 'python314.zip', 'python314._pth', 'LICENSE.txt']:
            if not (stage / required).is_file():
                raise ValueError(f'Embedded Python is missing {required}')
        for binary in stage.iterdir():
            if binary.suffix.lower() in ['.exe', '.dll', '.pyd'] and common.pe_arch(binary) != 0x8664:
                raise ValueError(f'Not Windows x64: {binary}')
        shutil.copy2(worker, stage / 'rsa306.exe')
        (stage / 'rsa306.exe').chmod(0o644)
        # The stdlib archive already contains bytecode. Worker invocation uses
        # -B to avoid cache writes, and -I to isolate user Python installations.
        common.write_runtime_manifest(stage, {
            'target': 'win32-x64', 'python_version': p['python_version'],
            'source_url': p['python_url'], 'source_sha256': p['python_sha256'],
            'executed_on_target': False,
            'native_worker': {**native_metadata, 'sha256': common.digest(worker),
                              'architecture': 'x64', 'transport': 'cyusb3-direct',
                              'dependencies': dependencies, 'hardware_qualified': False},
        })
        common.publish_runtime(stage, args.output, args.replace)


if __name__ == '__main__':
    main()
