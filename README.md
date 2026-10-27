# Tektronix RSA306B for SoundBase

A spectrum-analyzer plugin built from the official SoundBase template, with
shared native acquisition on Apple silicon and Windows x64. Windows retains an
optional Tektronix API fallback.

**Development candidate — not yet qualified for public release.** Native
acquisition over **9 kHz–6.2 GHz**, controls and lifecycle checks passed on the
same CI package: [54 fresh traces on Windows](docs/windows-native-validation-2026-10-27.md)
and [54 on Mac arm64](docs/macos-native-regression-2026-10-27.md). A Windows
development package also passed physical disconnect/reconnect and a 30-minute,
2,877-trace soak. SignalVu acquired normally after the native tests.

The [RF validation record](docs/rf-validation-2026-10-27.md) preserves the
completed generator and modulated-signal measurements and numerical replay.
These results have distinct artifact identities and measurement limits;
qualification of one final ZIP is still pending. See the
[current status](docs/implementation-status.md) and
[remaining release checks](docs/rsa306b-validation.md#remaining-release-checks).

| Platform | Runtime | Tested device and backend |
| --- | --- | --- |
| macOS 14+, arm64 | Bundled C++ worker and libusb | RSA306B HW7, firmware 1.7, FPGA 2.1 |
| Windows x64 | Bundled native worker; existing Tektronix RSA USB driver. Bundled Python for optional API fallback | Native: RSA306B HW7 / 1.7 / 2.1; fallback reference API 5.8.0275.0 |

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
compatible Tektronix RSA USB driver (also supplied with SignalVu-PC). The native
backend does not need RSA_API.dll or a separate Python installation. The API
fallback requires the x64 API as well. Keep an existing working SignalVu driver;
do not replace it with WinUSB or Zadig. SignalVu can remain installed, but must
be closed during plugin acquisition.
[Vendor downloads](https://www.tek.com/en/support/datasheets-manuals-software-downloads?series=RSA306B+USB+Spectrum+Analyzer)
provide the software and its installation instructions.

Select the analyzer by its serial number in SoundBase's live-scan picker.
Settings and traces use the standard SoundBase controls; the plugin has no
separate UI. Native acquisition exposes RF attenuation and IF gain on both
platforms, with fixed gain in the LF path. The Windows API fallback exposes a
Reference level control. SoundBase handles hold and
average trace modes. Incomplete, flagged, or incorrectly calibrated sweeps are
withheld rather than filled with invented data.

The **Windows acquisition backend** setting defaults to **Native USB**. Stop
and close live scans before switching to the API fallback. Its optional
**Tektronix API path (Windows fallback)** accepts an absolute DLL path or
directory. Leave it empty for standard installed locations. Advanced
worker/Python path overrides are also explicit plugin settings; device selection
never depends on a machine-local settings file.

Missing runtime/API prerequisites appear as **Needs setup**, with instructions.
The plugin periodically rechecks them. No analyzer attached is a normal state,
not a setup failure.

This beta uses ad-hoc macOS signing and an unsigned Windows worker. It is not
notarized or publisher-signed. Installation must work under the target machine's
normal security policy; the installer does not disable system protections.

## Development

The unified implementation is maintained on `main`. Both platform runtimes are
built from this source; there is no separate Windows source branch to install.

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
