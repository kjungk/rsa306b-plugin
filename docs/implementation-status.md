# Implementation and release status

Updated **27 October 2026** for plugin **0.1.0**.

The unified native implementation is merged into `main` and pushed to GitHub.
Both macOS arm64 and Windows x64 use the native backend by default. Windows
keeps the existing Tektronix RSA USB driver and offers an explicit vendor-API
fallback. The implementation is a **development candidate**; no public release
or SoundBase Lab approval has been completed.

## Completed validation

| Evidence | Completed result | Scope |
| --- | --- | --- |
| [Windows native tests](windows-native-validation-2026-10-27.md) | Development package: acquisition, controls, lifecycle, physical disconnect/reconnect and a 2,877-trace, 30-minute soak | Development ZIP `acc6deaa…`, LLVM-MinGW worker; not a publishable artifact |
| [Windows CI-package tests](windows-native-validation-2026-10-27.md#ci-candidate-hardware-results-and-vendor-handoff) | 54 fresh traces across acquisition, controls and lifecycle; official API acquired afterward and the operator confirmed a live SignalVu spectrum | CI ZIP `2eb4df2c…`, MSVC worker; SSH administrator token on TPX1 |
| [Mac transport regression](macos-native-regression-2026-10-27.md) | 54 fresh traces across the same suites; clean process exit and unchanged package files | The same CI ZIP `2eb4df2c…`, arm64 worker |
| [Earlier Mac acceptance](macos-validation-2026-10-26.md) | Acquisition, physical reconnect, 30-minute soak and Desktop checks | An earlier package; its results do not qualify newer bytes |
| [RF measurements and replay](rf-validation-2026-10-27.md) | 420 SMHU bursts replayed with exact Mac numerical agreement; three representative bursts agreed on Windows within 1.211×10⁻¹¹ dB; completed modulated-signal campaign retained | Working-reference measurements and software agreement; absolute accuracy remains unverified |

The shared CI artifact was built from clean commit
`b3735bfe77f62802b73a3b66309395911b6eff5d` by
[run 40490219354](https://github.com/kjungk/rsa306b-plugin/actions/runs/40490219354).
Its inner ZIP SHA-256 is
`2eb4df2cd7c0f486e8050aece66cf64cbb141a77a807695f85438afabd14c971`.
All 11 CI jobs passed, and the extracted package's real-worker mocks passed on
both targets. The local packaging suite subsequently passed 15 tests, including
the Windows metadata UTF-8 fix.

Commit `d3c59e5` added acceptance reports, the expected-contention classification
fix in the hardware client, and explicit UTF-8 decoding in the Windows builder.
The builder fix corrects generated metadata, not the worker's already-correct
UTF-8 output. These changes and later documentation updates do not alter the
retained CI ZIP. A new package has a new identity and needs its own qualification.

## Before public release

The [remaining release checks](rsa306b-validation.md#remaining-release-checks)
cover final-package soak/reconnect, Desktop rendering and lifecycle, ordinary
Windows user permissions and clean-PC setup, targeted RF confirmation and
documented measurement limits. Then complete the
[immutable-candidate publication workflow](../release/README.md), Lab submission,
review and installation checks. `release/qualification.json` remains pending.

The driver is unchanged and SignalVu handoff passed for sequential use. This
does not establish simultaneous acquisition or a driver-free Windows installer.
Raw private recordings and calibration dumps remain outside public source and
release assets.

## Historical implementation record — 26 October 2026

The following results retain the original local candidate and Windows API test
identity. They predate the native Windows backend and must not be used to
qualify it. During that initial implementation, no plugin was installed in
SoundBase, no GitHub/Lab release was published, and the original `tektronix-ui`
repository was not modified.

### Candidate

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
The source included a separate opt-in hardware-check client; it is not a
plugin runtime dependency and does not change the candidate payload.

### Passed checks

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

### Windows API acquisition subset

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
