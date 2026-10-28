# Build and release

The plugin ZIP contains one `rsa306b/` folder with native macOS arm64 and
Windows x64 workers, libusb for Mac, and Python for the optional Windows API
backend. SoundBase supplies Node. Windows users install the compatible Tektronix
USB driver separately; the API backend also needs the x64 RSA API.

## Build prerequisites

Use Node 20+ and Python 3.11+. Install the pinned npm dependencies with `npm ci`.
Build each runtime on its own target:

- **Mac:** Apple silicon, Apple command-line tools and CMake 3.25+.
- **Windows:** x64, Visual Studio 2022 or newer C++ tools, Windows SDK and CMake
  3.25+ (4.2+ for Visual Studio 2026). CI uses CMake 4.2.3.

```sh
# Apple silicon
python3 scripts/build-native.py
```

```powershell
# Windows x64
python scripts/build-native-windows.py
```

Dependencies are pinned by URL and SHA-256 in `runtime-pins.json`. To build
offline, supply `--libusb-source PATH` on Mac or `--python-zip PATH` on Windows
with the matching archive. Both builders accept `--output`, `--replace`,
`--cmake` and `--ctest`. The Windows builder discovers an installed C++ toolchain;
`--generator` selects a particular compatible Visual Studio generator.

Both builds run offline CTest checks. Mac binaries are arm64-only, target macOS
14 and use relocatable libusb. Windows uses a static compiler runtime and rejects
non-system DLL dependencies. Generated files go under `runtime/`; build work goes
under `build/`. Neither directory belongs in Git.

## Check and package

```sh
npm run doctor
npm run manifest
npm test
npm run smoke
npm run test:packaging
node scripts/smoke.mjs --real-worker
npm run pack:release -- --out dist/candidate-01
```

The real-worker check uses mock input and needs the current target's built
runtime. Packaging needs both runtimes. The packer includes production npm
dependencies, user documentation, license notices and rebuildable native source.
It verifies runtime hashes, preserves executable permissions and refuses to
replace an existing ZIP. Use a new output directory for another build.

```sh
python3 scripts/verify-release.py dist/candidate-01/rsa306b-1.0.0.zip \
  --extract build/extracted-01 --output build/package-verification.json
node scripts/check-package.mjs build/extracted-01/rsa306b
```

Substitute the current version. The extraction directory must not exist.
The package check is hardware-free; Windows checks native and API workers in
mock mode. A dirty-source package is for development and cannot be published.

## Hardware acceptance

Use an extracted candidate whose files match its ZIP. Close other analyzer
applications and explicitly opt in:

```sh
node scripts/check-hardware.mjs build/extracted-01/rsa306b build/hardware.json \
  --hardware --archive dist/candidate-01/rsa306b-1.0.0.zip
```

The default mode checks acquisition ranges and retuning. Other modes are
`--mode controls`, `--mode lifecycle`, `--mode disconnect` and
`--mode soak --seconds 1800`. Disconnect mode prompts for physical USB removal
and reconnection. Each run needs a new report filename. Tests do not install the
plugin or modify a SoundBase project.

Check Desktop plot/waterfall, project reload, enable/disable, normal shutdown
and recovery. Windows checks include an ordinary unelevated user and sequential
SignalVu handoff with the supported driver. Record the scope of installation
checks; an existing driver installation does not establish clean-PC setup.
Mock checks do not establish RF accuracy.

## Publish the qualified ZIP

CI runs on `main`, pull requests and manual dispatch. It builds both targets and
checks the extracted package on each. Download its `plugin-zip` artifact and use
the inner `rsa306b-VERSION.zip`; the outer artifact has a different checksum.

Choose an unreleased version (`npm run bump -- patch` when needed), commit all
intended changes and freeze one clean-source CI ZIP. Documentation is packaged,
so later documentation edits also produce a different ZIP when rebuilt.

`qualification.json` binds release approval to the candidate version, source
commit and ZIP SHA-256. Its `macos_hardware`, `windows_hardware` and
`signal_generator` entries each require a passed JSON report beneath `release/`
and the SHA-256 of that report. Reports must match the candidate's artifact hash.
Each report is a release acceptance review. Distinguish checks on the candidate
from completed checks carried forward from an earlier build. Reuse prior
validation only after reviewing relevant source and dependency changes; record
the tested build, report hashes and reason it remains applicable. Do not label
earlier tests as tests of the current ZIP or treat a hardware-client report with
overall status `partial` as complete acceptance on its own. Retain untested scope
and known behavior explicitly. Raw measurements and device data stay private;
only concise acceptance summaries belong in `release/reports/`.

```sh
python3 scripts/verify-release.py dist/candidate-01/rsa306b-1.0.0.zip \
  --qualification release/qualification.json
```

Manually run **Release** with `publish: true` and the successful candidate's
`candidate_run_id`. It verifies qualification and publishes the existing bytes
as a stable release with the user-facing notes and qualification summaries,
without rebuilding. It refuses existing releases by default. Set `prerelease: true`
only for a beta release. With `publish: false`, it only rehearses the build.

Replacing a published version is an explicit maintainer operation. Leave
`replace_source` empty for normal releases. To replace an existing release,
enter its current full source commit; the workflow requires a mutable release,
an exactly matching tag and a newly qualified candidate. It updates the tag
using a lease, replaces the download assets and notes, and retains the previous
assets as a workflow artifact. Users must reinstall a replaced same-version ZIP;
the version number alone will not distinguish the two packages.

The plugin is already listed on SoundBase Lab. Submit a version update there
separately when requested, obtain approval and verify installation on both
targets with `SB_PLUGIN_DIRS` unset.

## Notices and replacement runtimes

The package includes the LGPL libusb source archive and build recipe. From its
`sources/` directory, run the Mac builder with
`--libusb-source ../runtime/darwin-arm64/sources/libusb-1.0.30.tar.bz2`.
Stop SoundBase before replacing a runtime. For a modified libusb, update the
source pin and build recipe; dynamic linking permits a replacement library.

Mac binaries use ad-hoc signing; Windows binaries are not publisher-signed.
Normal OS and enterprise policy applies. Installation does not disable those
protections. See [third-party notices](../THIRD-PARTY-NOTICES.md).
