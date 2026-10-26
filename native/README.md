# RSA306B native worker

This C++20 worker is imported from the original RSA306B implementation at
commit `20c7bc63c3612e1a2380b998efaf56b62429bb19`. The worker retains its MIT
license; imported and adapted original numerical helpers/tests are included
under this project's approved MIT scope in [LICENSE](LICENSE). No vendor DLL,
firmware, device calibration dump, private recording, or Swift application
source is included. libusb remains a separate LGPL dependency; the repository's
runtime builder supplies its pinned source, notices and relocatable arm64 build.

The direct USB implementation accepts only RSA306B **HW7, firmware 1.7,
FPGA 2.1**, with one attached analyzer and one session attempt per worker.
Its per-user lock preserves cooperation with the original RSA Monitor worker.
Discovery reads descriptors; `probe` opens and initializes the selected unit.

## Factory conversion status

`--level-unit dbm` selects the `hw7-fullrange-v1` factory model over
9 kHz–6.2 GHz. LF uses fixed VGA code 199 and block 2; RF uses switch route
0x71 and blocks 3–6. RF attenuation and IF gain do not apply to LF. The
calibration identity includes all five blocks and the device serial. Native
full-range planning splits at 22 MHz so calibration domains do not overlap.
Sub-resolution boundary fragments acquire a wider band inside their own
domain; the adapter still filters samples to the original requested edges.

The LF response uses the measured VGA/path tables, symmetric near-DC spline
and natural-spline normalization at the requested center. The earlier PCHIP
normalization left an approximately 0.000146 dB constant residual at off-grid
centers. The new C++ model matches all 226 retained, hash-verified official API
scale/EQ references, including 18 LF references. The LF comparison enforces a
1e-6 dB numerical tolerance and the RF comparison retains its existing 2e-4 dB
tolerance. These are software comparison tolerances, not instrument accuracy.

**Full-range hardware acceptance remains pending.** Capabilities explicitly
report `fullrange_hardware_validated: false` and
`absolute_accuracy_verified: false`. Synthetic tests and numerical comparison
do not establish physical accuracy, another hardware revision, or continuous
RF coverage. See [the validation record](../docs/rsa306b-validation.md).

## Build and offline checks

```sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build native/build -j 4
ctest --test-dir native/build --output-on-failure
```

A Homebrew libusb may target the current host OS. Such a development build
does not establish macOS 14 compatibility; release assembly rebuilds libusb
for the declared minimum OS. All CTest cases run offline, with synthetic
tables, captured protocol fixtures or mocks. None enumerate or open USB.

The private reference checker accepts an explicit evidence location:

```sh
python3 -B native/tools/check_native_calibration.py \
  --report /absolute/path/to/numerical-verification-02.json \
  --evidence-root /absolute/path/to/original-checkout \
  --tables /absolute/path/to/extracted-calibration-tables \
  --binary native/build/rsa_native_calibration_check \
  --output native/build/new-numerical-report.json
```

It verifies retained manifest, API record and table hashes, runs no vendor
code, and writes only the explicitly named new report. Private evidence is
not required for the public synthetic test suite and must not be packaged.

## Supervised worker interface

The existing commands remain `capabilities`, `discover`, `probe`,
`plan-sweep`, `sweep`, `capture`, `monitor`, `read-calibration`, `analyze-if`
and `analyze-iq`. Run `rsa306 --help` for their arguments.

Common flags:

- `--serial SERIAL`: compare identity immediately after initialization,
  before configuring or acquiring from the selected device.
- `--stdin-control`: accept `stop\n`, or EOF when the parent disappears.
  Cancellation unwinds acquisition and emits
  `{"type":"stopped","cleanup_complete":true}` only after session teardown.
  A blocked native call still requires the JavaScript supervisor's deadline
  and force-kill fallback; forced termination is not a clean stop.
- `--mock`: use generated analytic factory tables and generated ADC samples
  through the actual native FFT/calibration pipeline, without USB access.
  Serial is `MOCK-RSA306B`; identity and measurements explicitly carry
  `synthetic:true`, and spectra use `source:"synthetic-mock"`. This flag is
  for tests and cannot establish hardware acceptance.

JSONL stdout is reserved for data and lifecycle acknowledgments. Bounded
errors go to stderr. A sweep emits device identity, `sweep_begin`, ordered
`spectrum` segments and `sweep_end`; callers must reject incomplete, flagged,
wrong-unit, wrong-serial or missing-bin passes. Trace averaging and hold modes
belong to the SoundBase shell.
