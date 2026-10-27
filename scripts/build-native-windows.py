#!/usr/bin/env python3
"""Build/test the native x64 Windows worker and assemble both Windows backends.

Requires Windows x64, Visual Studio 2022 C++ tools + Windows SDK, CMake >=3.25,
and Python >=3.11. The native worker links the static MSVC runtime and Windows
inbox libraries only. CTest and capabilities never access physical USB.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location('runtime_common', Path(__file__).with_name('runtime-common.py'))
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)


def run(args):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default=os.environ.get('CMAKE_EXECUTABLE', 'cmake'))
    parser.add_argument('--ctest', default=os.environ.get('CTEST_EXECUTABLE', 'ctest'))
    parser.add_argument('--generator', default='Visual Studio 17 2022')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--python-zip', type=Path)
    parser.add_argument('--output', type=Path, default=common.ROOT / 'runtime/win32-x64')
    parser.add_argument('--replace', action='store_true')
    args = parser.parse_args()
    if platform.system() != 'Windows' or platform.machine().lower() not in ('amd64', 'x86_64'):
        parser.error('A native Windows x64 build host is required; cross-builds use CMake directly')
    if not args.generator.startswith('Visual Studio '):
        parser.error('Use an installed Visual Studio CMake generator for the MSVC release build')
    base = common.ROOT / 'build'
    base.mkdir(exist_ok=True)
    work = args.build_dir.resolve() if args.build_dir else Path(tempfile.mkdtemp(prefix='windows-native-', dir=base))
    work.mkdir(parents=True, exist_ok=True)
    print('Retained build directory:', work)
    run([args.cmake, '-S', common.ROOT / 'native', '-B', work,
         '-G', args.generator, '-A', 'x64', '-DBUILD_TESTING=ON',
         f'-DPython3_EXECUTABLE={sys.executable}'])
    run([args.cmake, '--build', work, '--config', 'Release', '--parallel', '4'])
    run([args.ctest, '--test-dir', work, '-C', 'Release', '--output-on-failure'])
    worker = work / 'Release/rsa306.exe'
    if common.pe_arch(worker) != 0x8664:
        raise ValueError('Build did not produce an x64 worker')
    caps = json.loads(subprocess.check_output([str(worker), 'capabilities'], text=True))
    caps.pop('lock_path', None)
    metadata = work / 'native-runtime-metadata.json'
    metadata.write_text(json.dumps({
        'compiler': 'MSVC', 'cmake_generator': args.generator,
        'static_compiler_runtime': True, 'sha256': common.digest(worker),
        'executed_on_target': True, 'offline_ctest': 'passed',
        'worker_capabilities': caps,
    }, indent=2) + '\n')
    command = [sys.executable, common.ROOT / 'scripts/assemble-runtime.py',
               '--native-worker', worker, '--native-metadata', metadata, '--output', args.output]
    if args.python_zip:
        command += ['--python-zip', args.python_zip]
    if args.replace:
        command += ['--replace']
    run(command)


if __name__ == '__main__':
    main()
