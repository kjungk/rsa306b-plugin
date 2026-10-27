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


def pe_imports(path):
    """Read PE32+ imports without running a Windows binary or external tools."""
    if pe_arch(path) != 0x8664:
        raise ValueError(f'Not a Windows x64 executable: {path}')
    data = Path(path).read_bytes()
    coff = struct.unpack_from('<I', data, 0x3c)[0] + 4
    if coff + 20 > len(data):
        raise ValueError('Truncated PE COFF header')
    sections, optional_size = struct.unpack_from('<H', data, coff + 2)[0], struct.unpack_from('<H', data, coff + 16)[0]
    optional = coff + 20
    if optional + optional_size > len(data) or optional_size < 128 or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('Invalid PE32+ optional header')
    section_offset = optional + optional_size
    if section_offset + sections * 40 > len(data):
        raise ValueError('Truncated PE section table')

    def address(rva, length):
        if rva < struct.unpack_from('<I', data, optional + 60)[0] and rva + length <= len(data):
            return rva
        for index in range(sections):
            virtual_size, virtual, raw_size, raw = struct.unpack_from('<IIII', data, section_offset + index * 40 + 8)
            if virtual <= rva < virtual + max(virtual_size, raw_size):
                offset = raw + rva - virtual
                if rva - virtual + length <= raw_size and offset + length <= len(data):
                    return offset
        raise ValueError('PE import RVA lies outside file-backed sections')

    imports_rva, imports_size = struct.unpack_from('<II', data, optional + 120)
    if not imports_rva:
        return []
    imports = []
    for index in range(min(imports_size // 20, 4096)):
        descriptor = struct.unpack_from('<IIIII', data, address(imports_rva + index * 20, 20))
        if not any(descriptor):
            return sorted(set(imports))
        name = address(descriptor[3], 1)
        end = data.find(b'\0', name, min(name + 256, len(data)))
        if end < 0:
            raise ValueError('Invalid PE import name')
        imports.append(data[name:end].decode('ascii').lower())
    raise ValueError('Unterminated PE import table')


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
