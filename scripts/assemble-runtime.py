#!/usr/bin/env python3
"""Assemble the pinned Windows x64 embeddable Python without executing it.

Example: python3 scripts/assemble-runtime.py --python-zip /path/to/pinned.zip
No Python packages, system drivers, vendor DLLs or firmware are installed.
"""
import argparse
import importlib.util
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
    parser.add_argument('--output', type=Path, default=common.ROOT / 'runtime/win32-x64')
    parser.add_argument('--replace', action='store_true')
    args = parser.parse_args()
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
        # The stdlib archive already contains bytecode. Worker invocation uses
        # -B to avoid cache writes, and -I to isolate user Python installations.
        common.write_runtime_manifest(stage, {
            'target': 'win32-x64', 'python_version': p['python_version'],
            'source_url': p['python_url'], 'source_sha256': p['python_sha256'],
            'executed_on_target': False,
        })
        common.publish_runtime(stage, args.output, args.replace)


if __name__ == '__main__':
    main()
