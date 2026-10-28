# Plugin architecture

SoundBase Desktop launches the Node plugin shell. The shell handles authenticated
loopback HTTP, discovery polling, device state, trace IDs and hold/average modes.
`adapter.js` selects the acquisition backend and delegates blocking device access
to supervised child processes.

| Component | Responsibility |
| --- | --- |
| `soundbase-plugin.json` | Plugin identity, supported targets and settings |
| `main.js` | SoundBase shell bootstrap and lifecycle hooks |
| `adapter.js` | Backend selection, prerequisite checks and discovery |
| `driver/native/` | Native worker supervision, configuration and sweep validation |
| `driver/windows/` | Optional Tektronix API backend through isolated Python |
| `native/` | C++ protocol, USB transports, calibration and spectrum processing |
| `runtime/` | Generated platform runtimes; excluded from Git |

## Device access

The default backend is native on both platforms. macOS uses libusb. Windows uses
SetupAPI and asynchronous `DeviceIoControl` with the installed Tektronix
RSA3USB3 driver; it loads neither the Tektronix API nor a Cypress userspace DLL. The optional
Windows API backend uses the separately installed x64 `RSA_API.dll`.
The USB driver is supplied by the SignalVu-PC or RSA API installer.

The only public plugin setting selects the Windows backend. Production always
selects the bundled native worker or bundled Python interpreter, and the API
fallback searches standard Tektronix installation locations. All path values
saved by older plugin versions are ignored. Low-level worker factories retain
overrides for offline tests; these are not forwarded from user configuration.
Discovered devices supply their serial automatically; the serial input is
marked `manualOnly`.

Native acquisition requires RSA306B HW7, firmware 1.7, FPGA 2.1 and SuperSpeed
USB. Device IDs derive from the serial number and every connection checks the
selected serial. One analyzer and one live connection are supported at a time.
No driver rebinding, firmware write or calibration write is performed.

## Configuration and spectra

Requests are clamped to supported values and the effective configuration is
returned to SoundBase. RF attenuation and IF gain are native controls; the API
backend exposes reference level. Below 22 MHz, native acquisition uses a separate
path with fixed gain. Mixed sweeps split at the route boundary.

The native worker applies the attached unit's factory calibration tables. The API
backend uses vendor conversion. Complete, finite and correctly labelled sweeps
reach SoundBase; invalid calibration, missing bins and flagged data withhold the
pass. Sequential segments retain their timing and quality information. Absolute
accuracy is unverified, and no interpolation conceals missing observations.

## Process ownership and cleanup

Prerequisite checks run after the startup handshake and retry after settings
changes or setup failures. Shutdown stops owned diagnostic, discovery and
acquisition workers. Cooperative stop precedes bounded forced termination;
an uncertain session is never reused.

Windows native and API workers share a named mutex. The driver can still allow
another application to open a handle, so this lock does not exclude SignalVu.
Other acquisition applications must be closed. Bulk transfers are delivered in
submission order; cancellation retains request storage until completion or
process exit.

The public core 1.4 SDK has no Desktop host-crash lifeline. Forcibly terminating
Desktop can leave a plugin process holding the device. The
[user manual](user-manual.md#stop-scanning-and-recover-the-analyzer) describes
recovery.

## Tests

`SB_RSA306B_MOCK=1` selects a scripted synthetic worker.
`SB_RSA306B_NATIVE_MOCK=1` uses the actual native worker with synthetic input.
`SB_RSA306B_BACKEND=darwin-arm64|win32-x64` selects the target during tests.
Production never substitutes synthetic data for a failed acquisition.

Contract, transport, calibration, spectrum and packaging regressions run without
hardware. The explicit `scripts/check-hardware.mjs --hardware` acceptance client
is separate and requires an attached analyzer. Build and check commands are in
[release preparation](../release/README.md).
