#!/usr/bin/env python3
"""Build/test the native x64 Windows worker and assemble both Windows backends.

Requires Windows x64, Visual Studio 2022 or newer C++ tools + Windows SDK,
CMake >=3.25 (>=4.2 for VS 2026), and Python >=3.11. The native worker links the static MSVC runtime and Windows
inbox libraries only. CTest and capabilities never access physical USB.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location('runtime_common', Path(__file__).with_name('runtime-common.py'))
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)


def run(args):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def select_visual_studio(instances, generators, requested=None):
    """Match installed C++ toolsets to generators this CMake actually supports."""
    supported = {}
    for entry in generators:
        name = entry.get('name', '')
        match = re.fullmatch(r'Visual Studio (\d+) \d{4}', name)
        if match and int(match[1]) >= 17:
            supported[name] = int(match[1])
    if requested is not None and requested not in supported:
        raise ValueError(f'CMake does not advertise the requested supported Visual Studio generator: {requested}')
    candidates = []
    installed = []
    for instance in instances:
        version = instance.get('installationVersion', '')
        path = instance.get('installationPath', '')
        if not re.fullmatch(r'\d+(?:\.\d+){1,3}', version) or not path:
            continue
        if instance.get('isComplete') is False or instance.get('isLaunchable') is False:
            continue
        parts = tuple(map(int, version.split('.')))
        installed.append(version)
        for name, major in supported.items():
            if parts[0] == major and (requested is None or requested == name):
                candidates.append((parts, path.casefold(), name, instance))
    if not candidates:
        raise ValueError('No installed Visual Studio 2022-or-newer C++ toolset matches CMake generators. '
                         f'Installed versions: {installed}; available generators: {sorted(supported)}. '
                         'Install the x64 C++ tools and a CMake version supporting that Visual Studio release.')
    _, _, generator, instance = max(candidates, key=lambda entry: (entry[0], entry[1]))
    return generator, instance


def detect_visual_studio(cmake, requested):
    installer = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    vswhere = str(installer) if installer.is_file() else shutil.which('vswhere')
    if not vswhere:
        raise ValueError('vswhere.exe is missing; install Visual Studio C++ tools with the Visual Studio Installer.')
    instances = json.loads(subprocess.check_output([
        vswhere, '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-prerelease', '-format', 'json', '-utf8',
    ], encoding='utf-8-sig'))
    capabilities = json.loads(subprocess.check_output([str(cmake), '-E', 'capabilities'], encoding='utf-8-sig'))
    return select_visual_studio(instances, capabilities['generators'], requested)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default=os.environ.get('CMAKE_EXECUTABLE', 'cmake'))
    parser.add_argument('--ctest', default=os.environ.get('CTEST_EXECUTABLE', 'ctest'))
    parser.add_argument('--generator', help='Override automatic matching of installed Visual Studio and CMake generators')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--python-zip', type=Path)
    parser.add_argument('--output', type=Path, default=common.ROOT / 'runtime/win32-x64')
    parser.add_argument('--replace', action='store_true')
    args = parser.parse_args()
    if platform.system() != 'Windows' or platform.machine().lower() not in ('amd64', 'x86_64'):
        parser.error('A native Windows x64 build host is required; cross-builds use CMake directly')
    try:
        generator, instance = detect_visual_studio(args.cmake, args.generator)
    except ValueError as error:
        parser.error(str(error))
    print(f'Selected {generator}: {instance["installationPath"]} ({instance["installationVersion"]})', flush=True)
    base = common.ROOT / 'build'
    base.mkdir(exist_ok=True)
    work = args.build_dir.resolve() if args.build_dir else Path(tempfile.mkdtemp(prefix='windows-native-', dir=base))
    work.mkdir(parents=True, exist_ok=True)
    print('Retained build directory:', work)
    run([args.cmake, '-S', common.ROOT / 'native', '-B', work,
         '-G', generator, '-A', 'x64', '-DBUILD_TESTING=ON',
         f'-DCMAKE_GENERATOR_INSTANCE={instance["installationPath"]}',
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
        'compiler': 'MSVC', 'cmake_generator': generator,
        'visual_studio_version': instance['installationVersion'],
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
