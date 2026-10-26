# RSA306B architecture

SoundBase Desktop starts the official Node plugin shell. The shell owns the
loopback HTTP contract, bearer authentication, discovery polling, state events,
trace IDs, long polling and trace-mode accumulation. `adapter.js` selects the
platform backend and delegates to independently killable workers in `driver/`.

| Layer | Responsibility |
| --- | --- |
| `soundbase-plugin.json` | One product, final stable ID, supported targets, explicit settings |
| `main.js` | Template bootstrap plus documented lifecycle hooks for prerequisite diagnostics and cleanup |
| `adapter.js`, `driver/` | Discovery, serial selection, clamping, effective settings, validated sweeps, worker supervision |
| `native/` | C++ USB acquisition, calibrated LF/RF processing and offline tests |
| `driver/windows/worker.py` | Isolated public Tektronix C API access through ctypes |
| `runtime/` | Generated arm64 worker/libusb and Windows x64 Python, never source-controlled |

## Identity and configuration

The plugin ID is `rsa306b`; its product is `plugin:rsa306b/rsa306b`. Device IDs
are serial-derived and stable across hosts. Discovery provides explicit serial
configuration; manual additions supply the serial through the same manifest
field. Every connection verifies the serial. An enumeration index is not saved
as the identity. Initial implementations accept exactly one attached analyzer.

Both adapters clamp requests to their actual capabilities and echo effective
settings. The public shell merges desired settings before calling the adapter;
HTTP RBW omission retains a prior manual value, while clearing manual RBW leaves
it absent in normalized adapter input. Automatic results belong under
`resolved.rbwHz`. Device-specific controls are merged by the shell and returned
with actual values. Windows reference level is a device control, avoiding the
public shell's limited top-level reference-level readback behavior.

## Measurement integrity

macOS uses the attached unit's factory LF/RF calibration. LF works on the direct
ADC frequency axis with fixed gain code 199; RF uses its supported route and
manual gain settings. Mixed sweeps split at 22 MHz. The full-range conversion
has a distinct provenance identity from the earlier RF-only implementation.
Windows uses the vendor API's dBm calculation and retains distinct vendor/API
provenance. Neither backend converts dBFS by applying an arbitrary constant.

Only complete, finite, correctly labelled sweeps reach SoundBase. Sequential
segments retain their timing and quality; missing bins, dropped data or invalid
calibration withhold the pass. No interpolation conceals missing measurements.
A displayed previous trace after a failed pass is not a newly acquired trace.
Independent absolute accuracy remains unverified until documented measurements
establish it for a stated unit, settings and frequency range.

## Process lifetime

Native loading and USB/API calls are kept out of the Node event loop. Prerequisite
checks run after the startup handshake and distinguish missing software from an
absent device. Diagnostics retry every 30 seconds and after a settings change;
stale checks must not replace results for newer settings. Shutdown clears timers
and terminates owned diagnostic, enumeration and acquisition workers.

Normal stop attempts cooperative cleanup before forced termination. Workers use
private stdin control/EOF for parent loss where supported, and remain ordinary
children rather than detached daemons. Core 1.4 does not provide a Desktop host
lifeline; an alive plugin does not learn that Desktop was force-killed. This
limitation is documented, not concealed by declaring unreleased core 1.5.

## Test modes

`SB_RSA306B_MOCK=1` selects an explicit portable scripted child for contract
checks. `SB_RSA306B_NATIVE_MOCK=1` exercises the actual target worker's mock mode.
`SB_RSA306B_BACKEND=darwin-arm64|win32-x64` selects a backend during tests.
Production never falls back to fake data after a runtime or USB failure. Missing
runtimes in a requested real-worker test fail the test instead of silently
skipping it.
