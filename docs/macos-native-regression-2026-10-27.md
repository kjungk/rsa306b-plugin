# Apple-silicon transport regression — 2026-10-27

The shared transport refactor passed acquisition, controls and lifecycle tests
on this Mac using the same committed-source CI ZIP tested on Windows. These
checks supplement the [26 October Mac acceptance](macos-validation-2026-10-26.md);
they do not transfer that older artifact's soak or Desktop qualification to a
new ZIP.

## Identity

| Item | Value |
| --- | --- |
| Host | macOS 26.6.2, build 25G83, arm64 |
| Runtime | SoundBase Desktop 2.26.0 bundled Node 24.21.0 |
| Analyzer | RSA306B HW7, firmware 1.7, FPGA 2.1 |
| CI run | [40490219354](https://github.com/kjungk/rsa306b-plugin/actions/runs/40490219354) |
| Source commit | `b3735bfe77f62802b73a3b66309395911b6eff5d`, clean |
| ZIP SHA-256 | `2eb4df2cd7c0f486e8050aece66cf64cbb141a77a807695f85438afabd14c971` |
| Mac worker SHA-256 | `37f16507955b63ad9b4f6dc2fdf1d7de8d4805a9c6aab128bef469608f1f8d66` |

The user closed SignalVu on TPX1 and moved the analyzer to this Mac. SoundBase
Desktop remained closed. The acceptance client launched the extracted plugin
in a separate Unicode-path test directory; it did not install or replace a
Desktop plugin or modify a SoundBase project.

## Results

| Suite | Fresh traces | Result |
| --- | ---: | --- |
| Acquisition | 17 | 470–694 MHz, LF, 22 MHz boundary, 9 kHz–6.2 GHz and active retune passed |
| Controls | 25 | RF/IF gain clamping and half-dB quantization, retained partial settings, manual/automatic RBW, hold/average modes and default restoration passed |
| Lifecycle | 12 | Three stop/start cycles, two close/reopen cycles and competing-owner rejection passed |

All **54 accepted traces** had fresh sweep IDs, finite values and the expected
geometry. One unchanged long-poll response during acquisition was excluded from
the fresh-trace count. Lifecycle testing emitted two expected failed-status
events for the deliberately competing alias; neither was a failure of the
active analyzer. No unexpected device failure, event-stream error or cleanup
error was recorded.

Each plugin process exited with code 0 after device cleanup. The final audit
found no remaining analyzer/plugin processes and no changes to any of the 999
checksummed payload files. The real-worker package mock also passed with this
ZIP on the Mac.

## Scope and evidence

This is a targeted hardware regression, not a new controlled-source amplitude
campaign or full release qualification. The [RF validation report](rf-validation-2026-10-27.md)
records the existing generator measurements and numerical bridge. Repeating
the final-package soak, physical disconnect/reconnect and Desktop checks,
including controlled hard-host-crash recovery, remains separate work.
`release/qualification.json` remains pending.

Private reports are retained in `.evidence/mac-native-regression-20261027/`:

| Report | SHA-256 |
| --- | --- |
| Acquisition | `bc8b421a5ee24a13ebadbbbe6a8060b45a23006d8e89a7f279e621b877488852` |
| Controls | `68d77e7f8f0ed62593006e6fabaa9327cc79acf88599d46c0096e74e487d1d6b` |
| Lifecycle | `ab58785048521e215335387d59c7a95692aa4e78ebd26a1af490f04cd3f0e732` |
