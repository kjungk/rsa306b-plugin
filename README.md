# Tektronix RSA306B for SoundBase

A spectrum-analyzer plugin built from the official SoundBase template, with a
native Apple silicon backend and a Windows backend using the Tektronix API.

**Development candidate — not yet qualified for public release.** The intended
range is **9 kHz–6.2 GHz** on both platforms. New macOS LF factory correction,
real-device acceptance of this package, and independent signal-generator
measurements must pass before publication. Synthetic tests do not establish RF
accuracy. See [validation and remaining gates](docs/rsa306b-validation.md).

| Platform | Runtime | Initial hardware qualification |
| --- | --- | --- |
| macOS 14+, arm64 | Bundled C++ worker and libusb | RSA306B HW7, firmware 1.7, FPGA 2.1 |
| Windows x64 | Bundled Python; separately installed Tektronix API/USB driver | RSA306B; initial reference host uses API 5.8.0275.0 |

Intel Mac, Windows arm64, Linux hardware operation and multiple connected
analyzers are outside this release. Both backends use sequential acquisition
bursts: wide scans are stitched observations with gaps, not simultaneous or
continuous monitoring across the entire span.

## Using the plugin

Read the [user manual](docs/user-manual.md) for installation, first-scan steps,
platform controls, troubleshooting and evaluation-package instructions.

Once the release is qualified and approved, install **Tektronix RSA306B** from
SoundBase Lab. SoundBase supplies the JavaScript runtime; recipients do not need
Node, Python, a compiler or Homebrew. Windows users must separately install the
official x64 Tektronix RSA API and compatible USB driver (or SignalVu-PC).
[Vendor downloads](https://www.tek.com/en/support/datasheets-manuals-software-downloads?series=RSA306B+USB+Spectrum+Analyzer)
provide the software and its installation instructions. Keep SignalVu and other
analyzer applications closed while using SoundBase.

Select the analyzer by its serial number in SoundBase's live-scan picker.
Settings and traces use the standard SoundBase controls; the plugin has no
separate UI. macOS exposes RF attenuation and IF gain, with fixed gain in the
LF path. Windows exposes a Reference level control. SoundBase handles hold and
average trace modes. Incomplete, flagged, or incorrectly calibrated sweeps are
withheld rather than filled with invented data.

The optional **Tektronix API path (Windows)** setting accepts an absolute DLL
path or its directory. Leave it empty for standard installed locations. Advanced
worker/Python path overrides are also explicit plugin settings; device selection
never depends on a machine-local settings file.

Missing runtime/API prerequisites appear as **Needs setup**, with instructions.
The plugin periodically rechecks them. No analyzer attached is a normal state,
not a setup failure.

This beta uses ad-hoc macOS signing and an unsigned Windows worker. It is not
notarized or publisher-signed. Installation must work under the target machine's
normal security policy; the installer does not disable system protections.

## Development

Node 20+ is required for development. Install the pinned public SDK:

```sh
npm ci
npm run manifest
npm test
npm run doctor
npm run smoke
npm run smoke:boot
```

`npm run smoke` explicitly selects a synthetic worker and requires a real-shell
trace. `smoke:boot` verifies startup without discovering or opening hardware.
Neither command measures RF. Build native runtimes using the commands in
[release preparation](release/README.md), then exercise the actual target worker
without hardware:

```sh
node scripts/smoke.mjs --real-worker
```

For local development, quit SoundBase and set `SB_PLUGIN_DIRS` to the **parent**
of this checkout before starting Desktop. This starts a real plugin unless an
explicit mock environment variable is set:

```sh
open -a "SoundBase Desktop" --env "SB_PLUGIN_DIRS=$(dirname "$PWD")"
```

Disable the old `rsa306b-native` or `rsa306b-windows` plugin while using this one.
The new ID is `rsa306b`; select the new device in existing projects. There is no
automatic project rewrite. Unset `SB_PLUGIN_DIRS` after installing through Lab so
two copies do not run.

See [architecture](docs/rsa306b-architecture.md),
[testing and hardware acceptance](docs/rsa306b-validation.md), and
[release preparation](release/README.md). The original template guide set remains
under [docs](docs/README.md).

## SDK compatibility and recovery

Dependencies are the public npm `@soundbase/plugin-shell` and
`@soundbase/plugin-contract` **0.12.2**, speaking core **1.4** and
SpectrumAnalyzer **1.2**. The similarly numbered copy inside some SoundBase
installations includes unreleased core 1.5 behavior; this plugin does not copy
those app dependencies or claim its host-crash lifeline.

Normal stop, disable and shutdown close owned workers. A worker timeout causes a
bounded forced termination and a device error. After a forced stop, reconnect
the analyzer before retrying. Forcibly killing Desktop may leave the core 1.4
plugin process holding the device: close the orphaned RSA306B plugin/worker using
the OS process manager, reconnect, and restart Desktop. Exact-package host-crash
behavior remains part of manual acceptance.

## Licensing and provenance

Original RSA306B code is MIT licensed; the SoundBase template and SDK retain
their Business Source License terms. See [LICENSE](LICENSE),
[LICENSE-TEMPLATE](LICENSE-TEMPLATE), and [UPSTREAM.md](UPSTREAM.md). Native and
runtime dependency notices travel with the release. Tektronix installers, DLLs,
firmware and private calibration dumps are not redistributed.

Installing this plugin runs code with your user account's privileges. Review
[the source](https://github.com/kjungk/rsa306b-plugin) and the maintainer before
installing. This is a community plugin, not a Tektronix-certified application.
