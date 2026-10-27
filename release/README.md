# Building and qualifying a release

The release is one runnable `rsa306b/` folder in one ZIP, containing native
workers for macOS arm64 and Windows x64, plus Python for the optional Windows
API fallback. SoundBase supplies Node. Windows users need the compatible
Tektronix RSA USB driver; only the API fallback also needs the installed x64
RSA API. A successful mock test is not hardware or amplitude-accuracy qualification.

## Current release position

The native Windows work and Mac regression results are merged into `main`.
Plugin 0.1.0 remains a development candidate. The
[implementation status](../docs/implementation-status.md) links the completed
hardware and RF reports; the
[remaining release checks](../docs/rsa306b-validation.md#remaining-release-checks)
define what is still needed for one qualified ZIP. No gate in
`release/qualification.json` is passed solely by merging the source.

The tested CI artifact from commit `b3735bf` remains unchanged. Later build,
test-client and documentation fixes are on `main`. The packer includes Markdown
documentation, so even a documentation-only rebuild produces different package
bytes. Finish the intended edits before freezing the next candidate, then keep
that ZIP and its checksum unchanged through qualification and publication.

## Assemble both runtimes

Use Python 3.11+ and Node 20+. The Mac build additionally needs Apple command-line
tools and CMake 3.25+. Run it on Apple silicon; it produces arm64-only binaries
with a macOS 14.0 minimum. The Windows build needs an x64 host, Visual Studio
2022 or newer C++ tools, a Windows SDK and CMake 3.25+ (4.2+ for Visual Studio
2026). CI uses CMake 4.2.3. It uses the static MSVC runtime
and Windows inbox libraries, with no Tektronix or Cypress SDK library linkage.
Dependencies are pinned by SHA-256 in
`release/runtime-pins.json`. Supplying the matching archives makes assembly
offline; omitting those arguments downloads only their pinned official URLs.

```sh
npm ci
python3 scripts/build-native.py --libusb-source /path/to/libusb-1.0.30.tar.bz2
# Run on the Windows x64 build host:
python scripts/build-native-windows.py --python-zip C:/path/to/python-3.14.9-embed-amd64.zip
```

Both commands accept `--output PATH` and `--replace`. Replacement is limited to
an existing generated runtime with a `runtime.json`. Both native builders also
accept `--cmake PATH` and `--ctest PATH` (or `CMAKE_EXECUTABLE` /
`CTEST_EXECUTABLE`). Build work/logs remain under `build/`; output goes under
`runtime/darwin-arm64/` and `runtime/win32-x64/` by default. These are generated
assets and are not committed. Native builds run offline CTest tests. The Mac
build checks architecture, load paths and ad-hoc signatures. The Windows build
checks x64 architecture and rejects non-inbox DLL dependencies. Use `--generator`
to select a particular installed Visual Studio CMake generator when needed.
Otherwise the builder queries `vswhere` for x64 C++ installations, matches their
versions to CMake's advertised generators, and selects the newest compatible
instance. It does not assume that a runner image name implies a particular
Visual Studio release.

Worker JSON is decoded explicitly as UTF-8 when generating Windows runtime
metadata. An earlier builder used the host code page and could garble the
human-readable capability limitation in `runtime.json`; the native worker bytes
and JSON protocol were already UTF-8. Correcting this metadata produces a new
package digest, so retain earlier acceptance evidence against its original ZIP.

For a separately built Windows worker, including a development cross-build,
`scripts/assemble-runtime.py --native-worker PATH --python-zip PATH` assembles
the worker and fallback Python without executing them. Such assembly does not
replace execution of the offline tests and package checks on Windows. Keep
compiler identity and target-test evidence distinct from the MSVC CI candidate.

## Pack a candidate

```sh
npm run doctor
npm run manifest
npm test
npm run smoke
python3 -m unittest discover -s tests/packaging -p 'test_*.py'
npm run pack:release -- --out dist/candidate-01
```

The packer includes both runtimes, public npm dependencies from the lockfile,
licenses, libusb corresponding source and a native rebuild recipe. It checks
runtime hashes before copying and hashes the final staged payload afterward.
The ZIP has stable timestamps and executable permissions. It refuses an existing
output filename; use a new `--out` directory for another candidate.

Outputs are `rsa306b-0.1.0.zip`, its `.sha256`, and `package-report.json` (substitute
the current version). The archive is extracted and checked, then booted without
enumerating hardware and exercised with an explicit worker mock. On a supported
host the real compiled/Python worker runs in mock mode; assembly on Linux runs a
portable mock, and requires the separate target jobs before claiming target
execution. `--expect 0.1.0` additionally requires matching versions and committed
source. A dirty local candidate is useful for development but cannot be published.

```sh
python3 scripts/verify-release.py dist/candidate-01/rsa306b-0.1.0.zip \
  --extract build/extracted-01 --output build/package-verification.json
node scripts/check-package.mjs build/extracted-01/rsa306b
```

Extraction destinations must not already exist. `check-package.mjs` is always
hardware-free: it checks the ordinary bootstrap and then explicitly enables mock
acquisition. On Windows it exercises both the native worker and the API fallback
without loading the Tektronix API. `--portable` selects the scripted mock for unsupported build hosts.
Use the real worker check on each target. Windows can use `python` instead of
`python3` for the verification command. Nothing installs into SoundBase's plugins
folder or changes drivers/security settings.

## CI and publication

CI runs Node tests on Linux, macOS arm64 and Windows x64; builds native runtimes;
assembles one candidate ZIP; and runs extracted-package mock checks on both
supported targets. The `plugin-zip` artifact is the exact candidate to qualify.
CI runs on pushes to `main`, pull requests and manual dispatch. Download and
extract the outer Actions artifact, then use its inner `rsa306b-VERSION.zip`;
the artifact-container digest is different from the inner plugin ZIP digest
recorded by `package-report.json` and the qualification reports.
The Release workflow defaults to the same rehearsal and never publishes merely
because a tag was pushed. CI stores native runtimes in tar files so artifact
transport preserves executable permissions.

Before qualification, choose an unreleased version. If a version change is
needed, use the template's `npm run bump`; then commit source, documentation and
lockfile on `main` and build a successful candidate. Perform macOS and
Windows hardware acceptance and targeted signal-generator confirmations using
that exact ZIP. The completed SMHU campaign and its numerical replay are retained
in [the RF validation record](../docs/rf-validation-2026-10-27.md); do not discard
that evidence or present it as a test of different package bytes. The generator
model, calibration, source uncertainty, cables, tested
frequencies/levels, LF/RF boundary behavior and untested range must be recorded.
The repository's `qualification.json` deliberately retains every gate as pending
until acceptance of one candidate is complete. Individual hardware-client
reports with `automated_status: passed` and overall `status: partial` are useful
evidence, but they do not satisfy a completed release gate by themselves.

Fill `release/qualification.json` with the candidate's `source_commit`, version
and `artifact_sha256` from `package-report.json`. Each of `macos_hardware`,
`windows_hardware` and `signal_generator` requires:

- `status: "passed"` only after that acceptance is actually complete;
- `report`: a relative path below `release/` to its JSON report;
- `sha256`: SHA-256 of the exact report bytes.

Each report must contain matching `artifact_sha256` and `status: "passed"`, plus
the actual measurements, scope and conditions. Keep raw private recordings,
credentials and per-device factory dumps outside the repository. Commit the
qualification summary and public-safe reports. They are outside the ZIP and do
not change the candidate being qualified.

```sh
python3 scripts/verify-release.py dist/candidate-01/rsa306b-0.1.0.zip \
  --qualification release/qualification.json
```

To publish, manually run **Release** with `publish: true` and the successful
candidate's GitHub Actions `candidate_run_id`. The workflow downloads the
previously qualified bytes, checks that run/source identity and every gate, and
creates a prerelease at the candidate's source commit. It does not rebuild the
candidate. Missing or mismatched evidence stops publication. Existing releases
are never overwritten. Exactly one ZIP is attached, plus checksum and qualification
summary. Then submit that public release to Lab with at least one screenshot,
wait for review, and verify Lab installation on each target with `SB_PLUGIN_DIRS`
unset.

## Rebuild or replace libusb

The package retains the LGPL libusb source archive and complete native worker
source. From the package's `sources/` folder, run the included native build script
against `../runtime/darwin-arm64/sources/libusb-1.0.30.tar.bz2`. It writes an
independent rebuilt runtime, which can replace the installed native runtime after
SoundBase is stopped. For a modified libusb, adapt the source digest/pin and build
recipe in that source tree; the runtime uses dynamic linking and ad-hoc signing.
No proprietary signing identity or vendor SDK is needed for the Mac build.

The beta Mac binaries are not notarized and Windows code is not publisher-signed.
Normal operating-system and enterprise policy still applies. Installation never
disables those protections. Forced Desktop termination is a separate limitation
of the currently published core 1.4 SDK; use the main README's recovery guidance.
