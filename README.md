# Tektronix RSA306B for SoundBase

A SoundBase Desktop plugin for the Tektronix RSA306B spectrum analyzer on
**Apple silicon Macs and Windows x64**. It provides live spectrum traces over
9 kHz–6.2 GHz, using SoundBase's scan controls, plot and waterfall.

**Development version:** public release qualification and SoundBase Lab approval
are pending.

## Requirements

| | macOS | Windows |
| --- | --- | --- |
| Computer | Apple silicon, macOS 14 or later | Windows x64 |
| Acquisition runtime | Native worker and libusb included | Native worker included; Tektronix RSA USB driver installed separately |
| Analyzer | RSA306B HW7, firmware 1.7, FPGA 2.1 | Same for native acquisition |
| Connection | USB 3 SuperSpeed | USB 3 SuperSpeed |

Intel Macs, Windows arm64, Linux hardware operation and multiple connected
analyzers are not supported. A complete plugin package needs no separate Node,
Python, Homebrew or compiler installation. SoundBase supplies Node.

The Windows **Native USB** backend uses the existing Tektronix driver without
`RSA_API.dll`. Keep a working SignalVu driver; do not replace it with WinUSB or
Zadig. SignalVu can remain installed, but must be closed during plugin acquisition.
The optional **Tektronix API** backend additionally requires the installed x64
RSA API and uses Python bundled with the plugin.

## Use the plugin

Read the [user manual](docs/user-manual.md) for installation, first-scan steps,
controls, SignalVu handoff, troubleshooting and evaluation-package loading.
GitHub's **Source code** downloads are not runnable plugin packages.

Select the connected analyzer by serial number in SoundBase's live-scan settings.
Native acquisition exposes RF attenuation and IF gain; the API fallback exposes
reference level. Stop and close the live scan before switching backends or using
another analyzer application.

Sweeps use sequential acquisitions with gaps. A wide scan is not simultaneous
coverage of its entire span. The native backend applies the device's factory
correction tables and reports dBm; absolute accuracy is not certified. Invalid
or incomplete sweeps are withheld, so an older trace may remain visible.

## Development

The unified source for both targets is on `main`. With Node 20+ installed:

```sh
npm ci
npm run doctor
npm run manifest
npm test
npm run smoke
npm run test:packaging
```

These checks use synthetic data and do not acquire from USB. Native integration
checks require the relevant runtime to be built first. See the
[build and release guide](release/README.md) and
[architecture](docs/architecture.md).

## License and support

Original plugin and native-worker code is [MIT licensed](LICENSE).
SoundBase-provided bootstrap and tooling retain their
[Business Source License](LICENSE-SOUNDBASE). Dependency notices accompany the
package; see [third-party notices](THIRD-PARTY-NOTICES.md).
Tektronix software, USB drivers and firmware are not redistributed.

This is a community plugin, not a Tektronix-certified application.
Report plugin problems in [GitHub Issues](https://github.com/kjungk/rsa306b-plugin/issues).
