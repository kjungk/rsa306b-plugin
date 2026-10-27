# Native Windows implementation and validation — 2026-10-27

This record covers completed local-development tests on TPX1 and initial
real-device tests of a separate CI candidate. The native worker accesses
the existing Tektronix RSA3USB3 driver directly and shares the same protocol, FFT
and factory-calibration code as macOS. The optional official-API backend remains
available. No driver replacement or vendor DLL redistribution is involved.

## Test identity

| Item | Value |
| --- | --- |
| Host | TPX1, Windows 11 Pro x64, build 26300 |
| Test execution | SSH administrator token; SoundBase Desktop 2.26.0 bundled Node 24.21.0 |
| Analyzer | RSA306B HW7, firmware 1.7, FPGA 2.1 |
| USB driver | RSA3USB3.sys 1.2.3.20; existing installation |
| Native compiler | LLVM-MinGW 20261020, cross-built from Apple silicon |
| Windows worker SHA-256 | `2cf637d454e8711c0c1a0d619b4a8f11437f5fe9bf5c3d136793c961643cc240` |
| Development ZIP SHA-256 | `acc6deaab1cf32c8730008804ec1b53a049120f2686770bbc217d601a3ea0713` |
| ZIP contents | 999 files: 998 checksummed payload files plus `SHA256SUMS`; both target runtimes |
| Compressed ZIP size | 15,648,453 bytes |
| Package provenance | Local development source, deliberately `source_dirty: true` |

This development ZIP is not a publishable release. Its hardware results below
must not be attributed to the CI candidate. The earlier 26 October Mac and
Windows API reports also describe different artifacts.

## Committed-source CI candidate

[CI run 40490219354](https://github.com/kjungk/rsa306b-plugin/actions/runs/40490219354)
completed all 11 jobs successfully. The downloaded inner plugin ZIP passed
`verify-release.py`, including its content inventory and clean-source provenance.

| Item | Value |
| --- | --- |
| Source commit | `b3735bfe77f62802b73a3b66309395911b6eff5d` |
| Source state | `source_dirty: false` |
| CI ZIP SHA-256 | `2eb4df2cd7c0f486e8050aece66cf64cbb141a77a807695f85438afabd14c971` |
| ZIP contents | 1000 files: 999 checksummed payload files plus `SHA256SUMS`; both target runtimes |
| Compressed ZIP size | 15,363,050 bytes |
| Windows compiler | MSVC, Visual Studio 18 2026, static compiler runtime |
| Windows worker SHA-256 | `36d2aaf893bd8e324428f3afc8bb909753b3c582c8afa10112fd57647dadd9a6` |
| Hardware acceptance | Acquisition, controls and lifecycle suites passed; remaining gates below |

CI establishes build, offline-test and package consistency. The exact downloaded
ZIP also passed `check-package.mjs` with real workers in mock mode on both Mac
and Windows, using SoundBase's bundled Node 24 runtime. Real-device results for
these bytes are recorded separately below; development-candidate reconnect and
endurance results do not qualify this CI artifact automatically.

## Completed offline and initial hardware checks

- Mac rebuilt with pinned libusb: five CTest groups passed, arm64/macOS 14 minimum,
  relocatable dependencies and ad-hoc signatures verified.
- Windows: core/DSP, process mutex, protocol and transport C++ tests passed;
  32 Python/CLI checks passed, including exact-byte stop, CRLF and parent EOF.
- The current local packaging suite has 15 passing tests, including refusal of
  a Python-only Windows runtime, Visual Studio generator selection and UTF-8
  metadata decoding. The final encoding fix/test was added after this CI build:
  its runtime metadata contains a misdecoded en dash in capability text. This
  does not change the tested worker bytes; the retained CI ZIP was not edited.
- Both real-worker mock backends passed from the extracted Unicode-path Windows
  package using SoundBase's bundled Node runtime.
- Native discovery reported USB 3 at 5000 Mbps and the expected interrupt/bulk
  endpoints. Probe read the supported revision and factory calibration.
- First native RF capture at 600 MHz completed with clean quality flags and
  finite factory-corrected dBm. This initial test used the immediately preceding
  worker `6ab06518dfd357a9fa0f17e68beaf3ebbba126e191efbd0354a4341526cb432c`;
  subsequent development-package tests use the worker in the first identity table.
- During real native acquisition, the loaded-module list contained only the
  worker and Windows system DLLs. No RSA_API, CyAPI or Python module was loaded.

Three representative SMHU bursts (baseline and RF/IF gain changes) were replayed
on TPX1 after explicit owner approval to transfer that private subset. The
maximum level difference from the recorded Mac output was **1.211×10⁻¹¹ dB**;
frequency axis and ENBW differences were **0 Hz**. This checks Windows numerical
agreement, not a new generator acquisition. The full Mac bridge covers all 420
bursts; see [RF validation](rf-validation-2026-10-27.md).

## Development-candidate hardware results

The development ZIP passed 17 fresh acquisition traces across the microphone
band, LF, the 22 MHz transition, 9 kHz–6.2 GHz and active retuning; 25 control
traces covering gain clamping/quantization, RBW and hold modes; and 12 lifecycle
traces across repeated stop/start and close/reopen. The in-plugin competing-owner
guard also rejected a second connection.

A dedicated contention check rejected both a second native worker and the API
worker while native acquisition continued, then confirmed cooperative cleanup.
**The driver did not enforce exclusive file-handle sharing:** an independent
open-only probe succeeded despite the native worker requesting share mode zero.
That probe issued no USB commands; another native sweep still arrived afterward.
The shared mutex protects cooperating plugin workers, not SignalVu. Other
acquisition applications must remain closed; concurrent use is unsupported.

Physical unplug/reconnect passed: three fresh sweeps before removal, failed
device status after removal, responsive shell and empty discovery while detached,
then rediscovery of the same serial and three fresh sweeps after reconnection.
Native acquisition resumed after USB power-up without starting a vendor app.
The development ZIP's 30-minute endurance test passed: **2,877 fresh sweeps in
1,800.408 seconds**, covering 470–694 MHz with 1001 output points, effective
5126.953125 Hz RBW, 0.5 dB RF attenuation and 11 dB IF gain. There were zero stale
or empty responses, zero failed-device events and no missing-sweep warnings.
The maximum reported trace interval was **1,479 ms**, including startup. These
are sequential sweeps with RF gaps, not continuous RF observation.

The soak harness cleaned up the device, then terminated the Windows plugin
process tree; its recorded plugin exit code is 1. This is not evidence of normal
Desktop shutdown. Individual automated reports retain overall status `partial`
while their tested suites have `automated_status: passed`.

The development soak report SHA-256 is
`d8fabe3545a2157d2073ef664ef0e7ca7a87d157d3a21176dcfae4febe3202aa`.

## CI-candidate hardware results and vendor handoff

The exact CI ZIP `2eb4df2c…` was extracted on TPX1 and tested with the same
SSH administrator token and SoundBase-bundled Node runtime:

| Suite | Fresh traces | Result |
| --- | --- | --- |
| Microphone band, LF, LF/RF boundary, full range and active retune | 17 | Passed |
| Gain clamping/quantization, RBW, max/min hold and average | 25 | Passed |
| Repeated stop/start, close/reopen and competing-owner rejection | 12 | Passed |

All **54 fresh traces** passed. Acquisition and controls recorded no failed-device
events. The lifecycle report's two failed status events were the expected rejection
of the deliberately competing alias; no unexpected device failures or missing-sweep
warnings occurred. During the slower full-range sweeps the client excluded nine
responses containing the previous sweep while waiting for new data; these were
not counted as fresh traces. No empty responses were recorded. Each suite cleaned
up the device before the harness terminated the plugin process tree, with exit
code 1 recorded; normal Desktop exit remains a separate test.

After native streaming and CI-package testing, the installed official Tektronix
API 5.8.0275.0 successfully acquired **three fresh sweeps in 6.941 seconds**,
returned the expected analyzer identity, emitted `stopped` and exited 0.
This confirms acquisition through the official API after native cleanup. The
operator then opened SignalVu and confirmed that it displayed a live spectrum.
Native-to-SignalVu handoff therefore passed. This verifies sequential use after
cleanup, not simultaneous acquisition by both applications.

The final file audit found all **998 development payload files** and **999 CI
payload files** unchanged. These counts exclude the archive's `SHA256SUMS` file.
The installed driver SHA-256 remained
`02f7f8b57d67ce0f9f5ea8d5b938243fb346bb74b9e3bb4c60f3a18bdb693372`.
At audit time, before the manual SignalVu check, no analyzer applications or
acquisition workers remained.

| Retained private report | SHA-256 |
| --- | --- |
| `ci-acquisition.json` | `866aff64db95db2b1aaafd80807ceed4b641ea99bbdd0bdf06b837b3950ee780` |
| `ci-controls.json` | `560cef23898bece2b0adbc2541740010032ab03d48b218decd9179c59379499b` |
| `ci-lifecycle.json` | `230ad6354a6bf8e5cbb425846a240b4d1cc6a19ded18d7b662e77be1e1711151` |
| `after-native.json` | `8407c4511d21438774667a5fd9c5dbac5f3dab9497fa1b3828345b0c35158464` |
| `final-file-integrity.json` | `5c69a8ba937f0ae55bd781934f23621668968c69600622b6364c6ae38b323e69` |

## Remaining qualification

Physical unplug/reconnect and the 30-minute soak passed on the development ZIP;
they have not been repeated on the MSVC CI ZIP. The same CI ZIP passed a
[54-trace Mac transport regression](macos-native-regression-2026-10-27.md).
Desktop plot/waterfall, project reload,
normal exit/enable/disable and controlled hard-host-crash recovery remain
unqualified for this candidate. The earlier RF campaign and numerical replay
remain useful evidence; targeted final-package RF confirmation and unmeasured
LF/high-frequency coverage retain the limits in [RF validation](rf-validation-2026-10-27.md).

The existing driver remains a one-time prerequisite on a clean Windows PC.
Normal unelevated Desktop operation, clean-PC installation and signed-driver
redistribution are not established by these tests. Driver files, firmware and
calibration dumps are not packaged.
`release/qualification.json` remains pending. Detailed private evidence is retained
under `.evidence/windows-native-20261027/`; transmitter-specific evidence is not
included in this report or the plugin ZIP.
