"""Small, dependency-free helpers for pinned runtime assembly."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import urllib.request

ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def pins(target):
    return json.loads((ROOT / 'release/runtime-pins.json').read_text())[target]


def source_file(given, url, expected):
    if given:
        path = Path(given).resolve()
    else:
        path = ROOT / 'build/downloads' / url.rsplit('/', 1)[-1]
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists():
            pending = path.with_suffix(path.suffix + '.partial')
            try:
                with urllib.request.urlopen(url, timeout=120) as response, pending.open('wb') as out:
                    shutil.copyfileobj(response, out)
                if digest(pending) != expected:
                    raise ValueError('Downloaded source digest does not match runtime-pins.json')
                pending.rename(path)
            finally:
                pending.unlink(missing_ok=True)
    if not path.is_file() or digest(path) != expected:
        raise ValueError(f'Pinned source SHA-256 mismatch: {path}')
    return path


def safe_member(name):
    p = PurePosixPath(name)
    if not name or p.is_absolute() or '..' in p.parts or '\\' in name or ':' in name:
        raise ValueError(f'Unsafe archive member: {name}')
    return p


def pe_arch(path):
    data = Path(path).read_bytes()
    if data[:2] != b'MZ' or len(data) < 64:
        raise ValueError(f'Not a PE executable: {path}')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if offset + 6 > len(data) or data[offset:offset + 4] != b'PE\0\0':
        raise ValueError(f'Invalid PE header: {path}')
    return struct.unpack_from('<H', data, offset + 4)[0]


def write_runtime_manifest(directory, metadata):
    files = {}
    for p in sorted(directory.rglob('*')):
        if p.is_symlink():
            raise ValueError(f'Runtime symlink is not portable: {p}')
        if p.is_file():
            files[p.relative_to(directory).as_posix()] = digest(p)
    (directory / 'runtime.json').write_text(json.dumps({
        'schema_version': 1, **metadata, 'files_sha256': files,
    }, indent=2) + '\n')


def publish_runtime(stage, output, replace):
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        if not replace:
            raise ValueError(f'Runtime already exists: {output}; use --replace to replace generated assets')
        if output.is_symlink() or not (output / 'runtime.json').is_file():
            raise ValueError(f'Refusing to replace a directory that is not an assembled runtime: {output}')
        shutil.rmtree(output)
    shutil.move(str(stage), str(output))
    print(f'Assembled {output}')
