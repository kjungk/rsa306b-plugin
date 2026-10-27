# Validation and public-release gates

This repository is a development candidate with completed native hardware tests
on both targets. The [current status](implementation-status.md) identifies each
tested artifact. The [completed RF measurements and numerical replay](rf-validation-2026-10-27.md)
preserve the source-project measurement evidence. Final release qualification
must bring the required checks together against one immutable ZIP.

## Evidence available on 27 October 2026

| Check | Result and artifact scope |
| --- | --- |
| Windows native acquisition, controls and lifecycle | Passed on development and CI packages; the CI package delivered 54 fresh traces |
| Mac native acquisition, controls and lifecycle | Passed 54 fresh traces on the same CI package, including full-range acquisition |
| Windows physical disconnect/reconnect and 30-minute soak | Passed on the development package; 2,877 fresh soak traces |
| Official API and SignalVu after native streaming | Official API acquired three fresh sweeps; operator confirmed SignalVu's live spectrum; driver unchanged |
| Earlier Mac soak, reconnect and Desktop checks | Passed on the 26 October package; see its report for the forced-host-exit limitation |
| RF evidence | Completed SMHU and modulated-signal measurements, 420-burst Mac replay and a three-burst Windows numerical comparison |

The [Windows report](windows-native-validation-2026-10-27.md),
[Mac regression report](macos-native-regression-2026-10-27.md),
[earlier Mac report](macos-validation-2026-10-26.md) and
[RF report](rf-validation-2026-10-27.md) retain the exact hashes and limits.

## Remaining release checks

1. Freeze a clean, successful CI candidate from `main` after the intended source
   and documentation changes. Use its exact ZIP for the remaining qualification;
   retain prior evidence against its original artifact rather than changing hashes.
2. Complete final-package soak and physical disconnect/reconnect on both targets,
   plus Desktop plot/waterfall, project reload, disable/enable and normal exit.
   Record controlled hard-host-crash behavior and the core 1.4 recovery limitation.
3. Validate Windows Desktop operation with an ordinary unelevated user and on a
   clean PC with the vendor USB driver installed through its official installer.
   The TPX1 SSH tests used an administrator token. A bundled driver installer is
   outside the current package; redistributing one requires separate validation.
4. Perform targeted final-package RF confirmation using the retained measurement
   evidence. Document source/cable uncertainty and unmeasured LF, route-transition
   and high-frequency coverage; do not turn tuning-range checks into an absolute
   amplitude-accuracy claim.
5. Complete and hash the qualification reports, promote the qualified ZIP using
   the release workflow, then submit to Lab with a screenshot, obtain approval
   and check installation on both targets with development overrides removed.

## Automated checks

From the repository root:

```sh
npm ci
npm run manifest
npm run doctor
npm test
npm run smoke
npm run smoke:boot
npm run test:packaging
```

The doctor may warn about the documented lifecycle-hook additions in `main.js`.
Do not change its reference hash to hide that warning. Check that the diff is
limited to lifecycle delegates, with the official imports/bootstrap intact.

Build the target runtimes using [release preparation](../release/README.md),
then run `node scripts/smoke.mjs --real-worker`. Run native CTest and Python
calibration tests as described by the native build. Hardware-free checks must
cover the actual child-process path, auth/health/handshake, discovery/open races,
clamping and auto transitions, finite complete traces, configuration readback,
LF/RF boundary geometry, malformed records, stalls and bounded stop/cleanup.

Private saved recordings can be used for additional numerical comparisons, but
are never required for public CI or packaged with the plugin. Record source and
artifact hashes with every retained validation report. A skipped private-data
comparison must be reported as skipped, not passed.

## Real-device acceptance

For the automated acquisition subset, with the analyzer available and other
acquisition applications closed, run the opt-in client against an extracted
candidate:

```sh
node scripts/check-hardware.mjs build/extracted-01/rsa306b .evidence/hardware-new.json --hardware
node scripts/check-hardware.mjs build/extracted-01/rsa306b .evidence/controls-new.json --hardware --mode controls
node scripts/check-hardware.mjs build/extracted-01/rsa306b .evidence/lifecycle-new.json --hardware --mode lifecycle
node scripts/check-hardware.mjs build/extracted-01/rsa306b .evidence/soak-new.json --hardware --mode soak --seconds 1800
node scripts/check-hardware.mjs build/extracted-01/rsa306b .evidence/disconnect-new.json --hardware --mode disconnect
```

Create the report's parent directory first. The client refuses to overwrite a
report, disables mock modes and explicitly selects the native backend on both
platforms. The default `acquisition` mode checks full range, LF, the LF/RF
transition, microphone band and active retuning. `controls` checks native RF
attenuation and IF gain, clamping, partial updates, manual/automatic RBW and
hold/average modes. `lifecycle` checks competing-owner rejection, repeated stop,
restart and close/reopen. The deliberate competing alias emits failed-status
events: only that alias, the expected ownership message and the verified HTTP
rejection are classified as expected. Other failed-device events fail the run.
The API fallback requires separate targeted checks;
these modes do not test its reference-level control.

`soak` runs for 1800 seconds by default; shorter runs retain the 30-minute soak
gate as pending. `disconnect` waits for the operator to physically unplug at
`READY_UNPLUG`, then reconnect at `READY_RECONNECT`. It never automates USB
removal or driver changes. Time limits are bounded; `--max-seconds N` overrides
the overall test deadline. Full-range traces receive a separate deadline based
on their reported sweep duration, instead of an unsuitable 90-second limit for
the entire stage. Repeated cached trace IDs cannot count as fresh acquisitions.

Use `--archive PATH.zip` to record the candidate ZIP's SHA-256, after verifying
that the extracted folder matches that archive with `scripts/verify-release.py`.
The archive option records its digest; it does not compare folder contents.
Reports save completed stages, fresh-trace counts, stale responses and warning
events while running. Each report remains `partial` even when its selected
mode passes, with untested modes and manual gates explicitly pending. The client
does not install the plugin or edit a SoundBase project. Its Windows shutdown
closes the device cooperatively before terminating the remaining plugin process
tree; a cleanup error fails the run. The harness's Windows process-tree
termination is not evidence of normal Desktop shutdown.

Use the **exact extracted release candidate** on macOS arm64 and Windows x64.
Keep one RSA306B attached and close other programs that acquire it. Record OS,
SoundBase, API, device hardware/firmware/FPGA versions, serial, candidate SHA-256
and the test configuration.

1. Verify no-device startup, device appearance and serial selection. Check
   missing Windows driver instructions, optional vendor-API setup errors and
   automatic recovery after fixing setup.
2. Scan LF-only, 21.75–22.25 MHz across the route transition, 470–694 MHz and
   9 kHz–6.2 GHz. Confirm axis, points, dBm units and complete-pass timing.
3. Retune while running; change RBW and platform controls; test auto/manual RBW,
   hold/average modes, pause/restart and project reload.
4. Confirm live traces and waterfall visually inside Desktop on both systems.
5. Check competing-owner rejection, plugin disable/enable, unplug during a
   sweep, physical reconnect and recovery using a fresh worker.
6. Run a 30-minute soak, recording complete/withheld sweeps and failures.
7. Test normal Desktop exit and a separately controlled forced Desktop exit.
   Record any orphaned core 1.4 plugin/worker and required recovery precisely.

Forced termination is a failure to clean up normally, even if a later reconnect
works. Do not mark hardware acceptance passed while an unexplained stale axis,
invalid level, missing cleanup or acquisition failure remains.

## Signal-generator evidence and remaining measurements

The R&S SMHU working-reference campaign completed on 27 October: 14 recordings,
420 quality-clean bursts, five frequencies from 100 MHz to 1.5 GHz, level steps,
gain changes, tuning offsets, repeat baseline and RF off. All saved bursts were
replayed through the plugin's shared RF math with zero per-bin difference. The
[RF report](rf-validation-2026-10-27.md) records the results, hashes and limits.
The source calibration status and cable loss remain unknown; no production
correction or absolute-accuracy claim follows from those observations.

Reuse this evidence and perform targeted final-package/Windows confirmations.
LF, the 22 MHz route transition and RF above 1.5 GHz are still unmeasured with a
controlled source. Confirm the source's coverage, calibration status,
amplitude/frequency uncertainty, 50-ohm setup, cable/attenuator losses and
operating conditions for additional measurements. Obtain suitable LF coverage
before claiming the new LF path accepted.

For outstanding coverage, use the relevant backends against the same physical
unit and source. Include 9–15 kHz
edge cases, 100 kHz, 1 MHz, 10 MHz and an off-grid LF center, 21.75 MHz, points
immediately below/above 22 MHz, microphone-band points (470, 600 and 694 MHz),
and representative higher RF points up to 6.2 GHz where the source permits.
Use several non-overloading levels and gains; retain source setting, corrected
input level, actual RBW/gain, measured level and uncertainty for each reading.
Use bin-centered CW or an appropriate integrated-power method so FFT scalloping
is not mistaken for calibration error.

Use the applicable conditions and limits in the
[Tektronix datasheet](https://download.tek.com/datasheet/RSA306B-Datasheet-EN-US-37W-60375-17.pdf).
The quoted amplitude limits apply under stated alignment, temperature, reference
level and SNR conditions; a manual-gain native measurement must not be labelled
manufacturer-certified merely because its error happens to fit one number.
Document a comparison budget including source/cable uncertainty and the actual
measurement method. Unexplained discrepancies block release. State measured
coverage separately from the supported tuning range; unmeasured regions remain
unverified.

## Immutable candidate and release decision

`release/qualification.json` remains pending. It binds acceptance to the candidate
ZIP SHA-256, source commit and version. The macOS hardware, Windows hardware and
signal-generator reports must all be passed, hashed and tied to that same ZIP
before the publication workflow accepts them. Never fill these entries from
mock tests or prior-package reports. Rebuilding a different candidate invalidates
qualification for the old one. Follow [the release workflow](../release/README.md)
for the exact fields and commands.
