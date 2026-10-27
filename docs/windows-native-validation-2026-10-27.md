# Native Windows implementation and validation — 2026-10-27

This record covers a local development candidate on TPX1. The native worker
accesses the existing Tektronix RSA3USB3 driver directly and shares the same
protocol, FFT and factory-calibration code as macOS. The optional official-API
backend remains available. No driver replacement or vendor DLL redistribution
is involved.

## Test identity

| Item | Value |
| --- | --- |
| Host | TPX1, Windows 11 Pro x64, build 26300 |
| Analyzer | RSA306B HW7, firmware 1.7, FPGA 2.1 |
| USB driver | RSA3USB3.sys 1.2.3.20; existing installation |
| Native compiler | LLVM-MinGW 20261020, cross-built from Apple silicon |
| Windows worker SHA-256 | `2cf637d454e8711c0c1a0d619b4a8f11437f5fe9bf5c3d136793c961643cc240` |
| Development ZIP SHA-256 | `acc6deaab1cf32c8730008804ec1b53a049120f2686770bbc217d601a3ea0713` |
| ZIP contents | 999 checksummed files, 15,648,453 bytes, both target runtimes |
| Package provenance | Local development source, deliberately `source_dirty: true` |

This ZIP is a test artifact, not a publishable release. The MSVC CI build and
qualification of a committed-source immutable candidate remain separate work.
The earlier 26 October Mac and Windows API reports describe different artifacts.

## Completed offline and initial hardware checks

- Mac rebuilt with pinned libusb: five CTest groups passed, arm64/macOS 14 minimum,
  relocatable dependencies and ad-hoc signatures verified.
- Windows: core/DSP, process mutex, protocol and transport C++ tests passed;
  32 Python/CLI checks passed, including exact-byte stop, CRLF and parent EOF.
- Ten packaging tests passed, including refusal of a Python-only Windows runtime.
- Both real-worker mock backends passed from the extracted Unicode-path Windows
  package using SoundBase's bundled Node runtime.
- Native discovery reported USB 3 at 5000 Mbps and the expected interrupt/bulk
  endpoints. Probe read the supported revision and factory calibration.
- First native RF capture at 600 MHz completed with clean quality flags and
  finite factory-corrected dBm. This initial test used the immediately preceding
  worker `6ab06518dfd357a9fa0f17e68beaf3ebbba126e191efbd0354a4341526cb432c`;
  subsequent package tests use the worker in the identity table.
- During real native acquisition, the loaded-module list contained only the
  worker and Windows system DLLs. No RSA_API, CyAPI or Python module was loaded.

Three representative SMHU bursts (baseline and RF/IF gain changes) were replayed
on TPX1 after explicit owner approval to transfer that private subset. The
maximum level difference from the recorded Mac output was **1.211×10⁻¹¹ dB**;
frequency axis and ENBW differences were **0 Hz**. This checks Windows numerical
agreement, not a new generator acquisition. The full Mac bridge covers all 420
bursts; see [RF validation](rf-validation-2026-10-27.md).

## Acceptance status

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
The 30-minute endurance test is in progress; its result will be recorded after
completion. SignalVu handoff following full native streaming is still pending.

The existing driver remains a one-time prerequisite on a clean Windows PC.
Clean-PC installation and signed-driver redistribution are not established by
these tests. Driver files, firmware and calibration dumps are not packaged.
`release/qualification.json` remains pending. Detailed private evidence is retained
under `.evidence/windows-native-20261027/`; transmitter-specific evidence is not
included in this report or the plugin ZIP.
