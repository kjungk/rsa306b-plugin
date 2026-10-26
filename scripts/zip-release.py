#!/usr/bin/env python3
"""Create a sorted ZIP with stable timestamps and preserved executable bits."""
import argparse
from pathlib import Path
import stat
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    with zipfile.ZipFile(args.archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(args.source.rglob('*'), key=lambda p: p.relative_to(args.source).as_posix()):
            if path.is_symlink():
                raise ValueError(f'Refusing symlink: {path}')
            if not path.is_file():
                continue
            name = 'rsa306b/' + path.relative_to(args.source).as_posix()
            info = zipfile.ZipInfo(name, (2026, 10, 26, 0, 0, 0))
            info.create_system = 3
            mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
            info.external_attr = (stat.S_IFREG | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, path.read_bytes())


if __name__ == '__main__':
    main()
