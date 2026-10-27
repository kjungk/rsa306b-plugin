# Third-party notices

The original RSA306B plugin and native worker are MIT licensed. That license does
not relicense the following components.

- **SoundBase plugin shell and contract 0.12.2** are BUSL-1.1 with the SoundBase
  Additional Use Grant. Their JavaScript source and full licenses are retained
  under `node_modules/@soundbase/`. The template's original license is retained
  as `LICENSE-TEMPLATE`. The grant permits developing and distributing plugins
  for SoundBase; it does not authorize use in SoundBase substitutes.
- **Zod** is MIT licensed. The exact version and integrity are recorded in
  `package-lock.json`; its source and license are in `node_modules/zod/`.
- **libusb 1.0.30** is LGPL-2.1-or-later. It is dynamically linked, and its license
  is in `runtime/darwin-arm64/libusb-COPYING.txt`. The complete, unmodified,
  hash-pinned source archive travels in `runtime/darwin-arm64/sources/`.
  The plugin's native source and build recipe are under `sources/`. Rebuilding
  permits replacing/relinking libusb; the local ad-hoc signature is not a
  signature-enforcement lock on a replacement library.
- **Python 3.14.9**, official Windows x64 embeddable distribution, retains its PSF
  License Agreement and bundled notices in `runtime/win32-x64/LICENSE.txt`.
  The original archive SHA-256 is
  `3c01d7cfc666f746a6f064f60fb2156ab0ec464d3d738ab796371a46a9bb24a2`.
  Source/release information is at
  <https://www.python.org/downloads/release/python-3149/>. The interpreter is
  unmodified; the optional Windows API worker runs with isolated imports and
  bytecode writes disabled. Native USB acquisition does not run Python.

Tektronix software is installed separately under its own terms. No vendor DLL,
installer, USB driver, firmware image, or per-device calibration dump is included.
