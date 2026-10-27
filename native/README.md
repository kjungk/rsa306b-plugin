# RSA306B native worker

A C++20 worker for RSA306B HW7, firmware 1.7 and FPGA 2.1. It uses libusb on
macOS arm64 and the existing RSA3USB3 driver on Windows x64. The worker handles
one connected analyzer and one session attempt per process.

Factory-corrected dBm covers the 9 kHz–6.2 GHz tuning range. Below 22 MHz the
worker uses a fixed-gain LF path; RF uses its requested attenuation and IF gain.
Sweeps crossing the boundary use both paths. Factory correction does not imply
certified absolute accuracy or continuous RF coverage.

## Build and test

Run from the repository root on Apple silicon with CMake and libusb available:

```sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build native/build -j 4
ctest --test-dir native/build --output-on-failure
```

For packaged runtimes, use `python3 scripts/build-native.py` on Apple silicon
or `python scripts/build-native-windows.py` on Windows x64. The Mac builder
rebuilds pinned libusb for macOS 14; a host-installed libusb may require a newer
OS. The Windows builder uses MSVC and a static compiler runtime. Both builders
run offline CTest checks before assembling their runtime.

Tests use synthetic samples, analytic calibration tables and fixed register
vectors. They do not enumerate or open USB. The calibration test helper under
`tools/` is built only when `BUILD_TESTING` is enabled.

## Worker protocol

Run `rsa306 --help` for supported commands. JSONL stdout carries identity,
capabilities, spectrum and lifecycle records; stderr carries bounded errors.
A sweep has `sweep_begin`, ordered `spectrum` segments and `sweep_end` records.
The caller rejects incomplete, flagged, wrong-unit or missing-bin passes.

`--serial SERIAL` verifies the selected unit before acquisition.
`--stdin-control` accepts `stop\n` and EOF. A clean teardown emits
`{"type":"stopped","cleanup_complete":true}`. Blocked calls still need the
supervisor's timeout and process-termination fallback.

`--mock` runs the actual spectrum/calibration pipeline on synthetic samples,
without USB. Output explicitly identifies synthetic data. Hold and average
trace modes belong to SoundBase, not this worker.

Original worker code is covered by the adjacent [MIT license](LICENSE).
