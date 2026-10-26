#!/usr/bin/env python3
"""Build a relocatable, ad-hoc-signed macOS arm64 worker and pinned libusb.

Requires Apple command-line tools, CMake >=3.25 and Python >=3.11.
Never opens USB: CTest and --mock checks are hardware-free.
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
import tarfile
import tempfile

spec = importlib.util.spec_from_file_location('runtime_common', Path(__file__).with_name('runtime-common.py'))
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)


def run(args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def output(args):
    return subprocess.check_output(list(map(str, args)), text=True).strip()


def metadata(binary, minimum):
    arch = output(['lipo', '-archs', binary])
    if arch != 'arm64':
        raise ValueError(f'Unexpected architecture: {arch}')
    matches = re.findall(r'\bminos (\d+\.\d+(?:\.\d+)?)', output(['otool', '-l', binary]))
    if matches != [minimum]:
        raise ValueError(f'Wrong minimum macOS for {binary.name}: {matches}')
    libraries = []
    for line in output(['otool', '-L', binary]).splitlines()[1:]:
        name = line.strip().split(' (', 1)[0]
        if not name.startswith(('/usr/lib/', '/System/Library/', '@executable_path/', '@rpath/')):
            raise ValueError(f'Non-relocatable dependency: {name}')
        libraries.append(name)
    run(['codesign', '--verify', '--strict', binary])
    return {'architecture': arch, 'minimum_macos': minimum, 'dependencies': libraries,
            'signature': 'ad-hoc', 'notarized': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--libusb-source', type=Path)
    parser.add_argument('--cmake', default=os.environ.get('CMAKE_EXECUTABLE', 'cmake'))
    parser.add_argument('--ctest', default=os.environ.get('CTEST_EXECUTABLE', 'ctest'))
    parser.add_argument('--output', type=Path, default=common.ROOT / 'runtime/darwin-arm64')
    parser.add_argument('--replace', action='store_true')
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        parser.error('A native Apple silicon macOS build host is required')
    p = common.pins('darwin-arm64')
    source = common.source_file(args.libusb_source, p['libusb_url'], p['libusb_sha256'])
    build = common.ROOT / 'build'
    build.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='native-runtime-', dir=build))
    print('Retained build directory:', work)
    with tarfile.open(source, 'r:bz2') as archive:
        for entry in archive.getmembers():
            common.safe_member(entry.name)
            if not (entry.isfile() or entry.isdir()):
                raise ValueError('Unexpected member in pinned libusb source')
        archive.extractall(work)
    usb = work / ('libusb-' + p['libusb_version'])
    prefix = work / 'libusb-prefix'
    env = dict(os.environ, MACOSX_DEPLOYMENT_TARGET=p['minimum_macos'], CC='clang',
               CFLAGS=f'-O2 -arch arm64 -mmacosx-version-min={p["minimum_macos"]} -ffile-prefix-map={work}=.',
               LDFLAGS=f'-arch arm64 -mmacosx-version-min={p["minimum_macos"]}',
               lt_cv_sys_max_cmd_len='32768')
    run(['./configure', f'--prefix={prefix}', '--disable-static', '--enable-shared',
         '--disable-dependency-tracking'], cwd=usb, env=env)
    run(['make', '-j', '4'], cwd=usb, env=env)
    run(['make', 'install'], cwd=usb, env=env)
    native = work / 'native-build'
    run([args.cmake, '-S', common.ROOT / 'native', '-B', native,
         '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON',
         '-DCMAKE_OSX_ARCHITECTURES=arm64', f'-DCMAKE_OSX_DEPLOYMENT_TARGET={p["minimum_macos"]}',
         f'-DCMAKE_CXX_FLAGS=-ffile-prefix-map={common.ROOT}=. -ffile-prefix-map={work}=.',
         f'-DLIBUSB_INCLUDE_DIR={prefix}/include/libusb-1.0',
         f'-DLIBUSB_LIBRARY={prefix}/lib/libusb-1.0.dylib'])
    run([args.cmake, '--build', native, '-j', '4'])
    run([args.ctest, '--test-dir', native, '--output-on-failure'])
    stage = work / 'darwin-arm64'
    stage.mkdir()
    worker, library = stage / 'rsa306', stage / 'libusb-1.0.0.dylib'
    shutil.copy2(native / 'rsa306', worker)
    shutil.copy2(prefix / 'lib/libusb-1.0.dylib', library)
    shutil.copy2(usb / 'COPYING', stage / 'libusb-COPYING.txt')
    (stage / 'sources').mkdir()
    shutil.copy2(source, stage / 'sources' / f'libusb-{p["libusb_version"]}.tar.bz2')
    run(['install_name_tool', '-id', '@rpath/libusb-1.0.0.dylib', library])
    linked = [line.strip().split(' (')[0] for line in output(['otool', '-L', worker]).splitlines()[1:] if 'libusb-1.0' in line]
    if len(linked) != 1:
        raise ValueError(f'Expected exactly one linked libusb: {linked}')
    run(['install_name_tool', '-change', linked[0], '@executable_path/libusb-1.0.0.dylib', worker])
    for binary in [library, worker]:
        binary.chmod(0o755)
        run(['codesign', '--force', '--sign', '-', binary])
    binary_metadata = {binary.name: metadata(binary, p['minimum_macos']) for binary in [worker, library]}
    caps = json.loads(output([worker, 'capabilities']))
    caps.pop('lock_path', None)
    common.write_runtime_manifest(stage, {
        'target': 'darwin-arm64', 'minimum_macos': p['minimum_macos'],
        'libusb_version': p['libusb_version'], 'source_url': p['libusb_url'],
        'source_sha256': p['libusb_sha256'], 'binaries': binary_metadata,
        'worker_capabilities': caps, 'hardware_qualified': False,
    })
    common.publish_runtime(stage, args.output, args.replace)


if __name__ == '__main__':
    main()
