# Implementation and validation record — 2026-10-26

The official-template RSA306B implementation is complete as a **development
candidate**. Public release qualification is pending. Nothing was installed into
SoundBase, published to GitHub/Lab, or changed in the original `tektronix-ui`
repository during this implementation.

## Candidate

Local artifact: `dist/candidate-02/rsa306b-0.1.0.zip` (15,039,412 bytes,
983 checksummed files), containing both `darwin-arm64` and `win32-x64` runtimes.

SHA-256:

```text
6084550655f69eeb93c53c3c0a1148b014afdcc03915954c9a5f7dea7911ab0f
```

This rehearsal candidate is intentionally stamped `source_dirty: true` because
it was built before the implementation changes were committed. Committing the
source does not change that ZIP or its qualification status; the publication
gate correctly rejects it. A committed-source CI candidate must be built and its exact bytes qualified
before public release; this record must not be used to approve a different ZIP.
The current source includes a separate opt-in hardware-check client; it is not a
plugin runtime dependency and does not change the candidate payload.

## Passed checks

| Check | Result |
| --- | --- |
| Public SDK manifest/schema | core 1.4, SpectrumAnalyzer 1.2, SDK 0.12.2 |
| JavaScript tests | 39 passed, zero failures or skips on the Mac development host |
| Packaging tests | 6 passed, including malformed archives and publication gates |
| Native CTest | All four groups passed with the packaged worker build |
| Saved official API numerical comparisons | 226 passed: 208 RF and 18 LF |
| Numerical EQ maximum residual | LF 1.336×10⁻⁷ dB; RF 1.903×10⁻⁶ dB |
| Bootstrap/smoke | Authenticated HTTP, handshake within 10 seconds, complete mock trace, cleanup |
| Native release binaries | Thin arm64, macOS 14 minimum, relocatable libusb, valid ad-hoc signatures |
| Extracted Mac candidate | Actual native worker mock, genuinely read-only directory, spaces/Unicode path, all 983 files unchanged |
| Extracted Windows candidate | Bundled Python actual-worker mock under SoundBase's Node 24.21.0, spaces/Unicode path |
| Windows real acquisition | All seven stages below passed; report deliberately remains partial |
| Windows cleanup | No candidate plugin/acquisition worker remained running; package files unchanged |
| Source hygiene | `git diff --check` passed; original source repository remained clean |

## Windows acquisition subset

The user confirmed the analyzer was available and other acquisition applications
were closed. Tests ran against the extracted candidate in a separate temporary
validation directory, not against the installed legacy plugin. No SoundBase
project was modified.

| Stage | Accepted traces |
| --- | ---: |
| 9 kHz–6.2 GHz | 3 |
| 470–694 MHz | 5 |
| 9 kHz–1 MHz | 3 |
| 21.75–22.25 MHz | 3 |
| Active retune to 590–600 MHz | 3 |
| Windows reference level −10 dBm | 3 |
| Fresh-worker restart | 3 |

These 23 traces establish acquisition, geometry, configuration and lifecycle for
this candidate on that Windows host. They do not establish independent amplitude
accuracy or visual correctness inside Desktop. Acquisition tests took about
109 seconds, including startup, configuration, retuning and cleanup.

Local detailed reports are retained in `.evidence/windows-candidate-02/` and
`build/mac-package-readonly-02.json`. Private numerical-comparison output is
retained under `native/build/`. Those local evidence files are excluded from
public source and release assets.

## Remaining release gates

- Real macOS acquisition with the new full-range LF correction, including cold
  startup, LF/RF transitions and exact-package comparison with known signals.
- Both-platform Desktop plot/waterfall checks, physical unplug/reconnect,
  30-minute endurance and controlled host-crash/recovery checks.
- The planned signal-generator measurements. Its model/calibrated coverage is
  still unknown; follow `rsa306b-validation.md` before claiming accuracy.
- A committed-source CI build and qualification tied to that immutable ZIP.
- Lab screenshot, submission, approval and installation checks on both targets.

`release/qualification.json` retains pending status for every full acceptance
gate. The Windows automated subset is not promoted to a full hardware pass.
