# Conducted RF evidence and native replay — 2026-10-27

The controlled RF measurements in the `tektronix-ui` source project are complete
and are relevant to this plugin. All **420 saved SMHU bursts** were replayed
offline through this repository's current shared RF spectrum and factory
calibration code. Every displayed frequency, ENBW and level matched the recorded
result exactly at the emitted double precision. This preserves the existing RF
measurement evidence through the plugin's LF extension; it does not qualify a
new Windows transport or an untested release ZIP.

## Completed working-reference measurements

The source project's `docs/SMHUValidation.md` records 14 acquisitions on an
RSA306B with HW7, firmware 1.7 and FPGA 2.1, using an R&S SMHU as the
operator-accepted practical reference. The campaign covers:

- 100, 470, 600, 800 and 1500 MHz at a −40 dBm generator display setting.
- −60/−40/−20 dBm at 600 MHz, receiver centers at 590/610 MHz, RF attenuation
  increased by 10 dB, IF gain reduced by 10 dB, repeat baseline and RF off.
- 420 complete bursts with no reported acquisition faults, no signed-ADC rail
  hits and verified raw-sample lengths and retained file hashes.

The five spot frequencies differed from the generator display by **−0.269 to
+0.712 dB**. The requested 40 dB source-level change measured **39.9661 dB**.
The RF attenuation and IF gain changes moved corrected power by **−0.0247 dB**
and **−0.0609 dB**, respectively. No correction offset was fitted.

These differences combine source, cable and analyzer behavior. The SMHU's
calibration status is unknown, cable loss was not measured, and the first
acquisition followed approximately five minutes of source warm-up. The later
repeat differed by −0.1203 dB over about 12 minutes. Frequency comparisons
included approximately +2.3 ppm combined source/receiver difference. These are
practical bench observations rather than isolated analyzer accuracy or long-term
stability specifications; `absolute_accuracy_verified` remains `false`.

CW power was integrated over 17 bins around the tone, with the periodic-Hann
bin-width/ENBW factor of 1/1.5. Peak-bin readings were retained separately to
avoid treating FFT scalloping as amplitude-calibration error.

The source project's `docs/ModulatedSignalValidation.md` also records **315
quality-clean spectra**: 15 locator spectra, 180 conducted monitor bursts and
120 wideband over-the-air bursts. Recorded spectra, averaging, max hold and
waterfall were inspected in RSA Monitor's offline rendering. This supports
acquisition and processing of real modulated signals. It does not establish
live SoundBase acceptance, continuous coverage, short-event detection or
certified transmitter power. The nominal attenuation chain was not independently
measured, and an initial wideband on/off average difference did not reproduce
consistently. Source identities, exact settings, measured powers, plots and raw
transmitter data remain private because one tested device is unreleased.

## Numerical bridge into this repository

The original `spectrum.cpp` is byte-identical to this repository's
`native/src/spectrum.cpp`. Inspection of the calibration changes shows the new
LF route and associated cache/identity changes; the RF formulas are retained.
The replay checks the actual output rather than relying on that inspection.

`native/tools/check_rf_replay.py` verifies each run's raw samples, spectra and
USB log against the campaign inventory. It reconstructs stored calibration
blocks from the logged request/response pairs, checks their CRCs, and requires
byte-for-byte agreement with the tables used for replay. The development-only
`rsa306_replay_rf_capture` tool then passes all saved signed ADC samples and
recorded settings through the current `if_spectrum()` and
`FactoryCalibration::levels()` functions. It never constructs a USB session.

| Check | Result |
| --- | --- |
| Recordings / spectra | 14 / 420 |
| Maximum frequency-axis difference | 0 Hz |
| Maximum window ENBW difference | 0 Hz |
| Maximum per-bin level difference | 0 dB |
| Calibration blocks 2–6 | Byte-identical to every recorded run |
| Hardware or final-package qualification | Not performed by replay |

The harness was built on Apple arm64 with Apple Clang 21.0.0, C++20 and `-O2`.
Malformed settings, truncated/extra raw data, non-finite points and numerical
comparison mismatches are covered by hardware-free tests. The same replay tool
was also built for Windows x64 with LLVM-MinGW 20261020. On TPX1, three
hash-inventoried first-burst slices (baseline and RF/IF gain changes) matched with
maximum level difference 1.211×10⁻¹¹ dB and zero axis/ENBW difference. The owner
explicitly approved that private subset transfer. This is compiler/runtime
agreement using saved samples, not a new Windows known-source measurement.

Retained identities:

| Item | SHA-256 / revision |
| --- | --- |
| Measured source revision | `20c7bc63c3612e1a2380b998efaf56b62429bb19` |
| Measured original native binary | `441dd133c80ef6e428601f8c7c768138f263502f4843225ec11aa3e62aee0ecc` |
| Original SMHU evidence inventory | `baf1e46dffd306612d54476d0e30577ee00a320945eef6d5e213ed918d3b93e3` |
| Replay binary | `ac16f090ab816a225f021fae042a5e15afcb2eb7bcd39c2721e86543b2c21d65` |
| Replay report | `b88f707a17ca300563a6b952549265030acb7cf7444754eaf593f28d85edca12` |
| Shared spectrum source | `afd6db9287ee64391796a9e7d078154211f633b7cec7aeeea30ab8fc85fb9d5f` |
| Shared calibration source | `172b54e519ea13eaa55aef9a20e290a0817800d42213959033fafe086e2792ba` |

The private report and detailed build/source provenance are retained under
`.evidence/rf-bridge-20261027/`. Original raw evidence remains in the source
project's ignored capture directories; it is not copied into this repository or
release archives. To repeat the bridge with available private inputs, build the
CTest target `rsa306_replay_rf_capture`, then run:

```sh
python3 -B native/tools/check_rf_replay.py \
  --manifest /path/to/smhu-2026-10-27/evidence-manifest-01.json \
  --tables /path/to/captured-calibration-blocks \
  --binary /path/to/rsa306_replay_rf_capture \
  --output .evidence/new-rf-replay.json
```

## Remaining scope

The completed SMHU and modulated campaigns should be reused, rather than listed
as measurements that have not happened. Remaining measurement coverage includes
LF and the 22 MHz route boundary, RF above 1.5 GHz, Windows known-level checks,
and any broader accuracy, thermal or vendor-detector-equivalence claims. A
short targeted confirmation on the final package is distinct from repeating the
whole source-project campaign.

Native transport and lifecycle hardware checks subsequently passed on
[Windows](windows-native-validation-2026-10-27.md) and
[Mac arm64](macos-native-regression-2026-10-27.md). The final immutable ZIP still
needs the remaining acceptance and report bindings in
[the validation plan](rsa306b-validation.md#remaining-release-checks); this report does not change
`release/qualification.json` from pending.
