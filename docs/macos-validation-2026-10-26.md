# macOS hardware validation 26 October 2026

This is the historical acceptance report for the package identified below.
For subsequent work, see the [27 October native transport regression](macos-native-regression-2026-10-27.md),
[completed RF measurements](rf-validation-2026-10-27.md) and
[current release status](implementation-status.md).

The CI candidate passed the Mac acquisition, controls, connection recovery and
30-minute endurance checks below. Desktop displayed real live spectrum and
waterfall data. A forced Desktop crash left the plugin running, as documented
for the public core 1.4 SDK; targeted cleanup and a fresh device connection
succeeded.

This is functional test evidence for one analyzer and one package. The generator
campaign and numerical bridge were documented the following day, with their
measurement limits. Neither this report nor those measurements qualify a
different ZIP automatically or establish absolute amplitude accuracy.

## Package and environment

| Item | Tested value |
| --- | --- |
| Plugin | Tektronix RSA306B 0.1.0 |
| CI | [Successful run 40359351319](https://github.com/kjungk/rsa306b-plugin/actions/runs/40359351319) |
| Source commit | `5708d7df5241857826aa946098ede64dc8d4ee3b`, clean source |
| ZIP SHA-256 | `967918a23ca964288ea9040c2c00be9266f3dc8a64113b28855b92ec38b38219` |
| Computer | Apple silicon, macOS 26.6.2 |
| SoundBase Desktop | 2.26.0, bundled Node 24.21.0 arm64 |
| Instrument | One RSA306B, HW7, firmware 1.7, FPGA 2.1 |
| Connection | USB 3, 5000 Mbit/s enumeration |

The downloaded Actions artifact matched GitHub's SHA-256 digest before
extraction. Tests used the extracted CI ZIP from a path containing spaces and
Unicode. All 983 files listed in its checksum manifest remained unchanged
through the automated tests. The native executable and libusb library were
arm64 binaries with valid ad-hoc signatures.

## Automated hardware checks

| Suite | Passed stages | Fresh traces checked | Scope |
| --- | ---: | ---: | --- |
| Acquisition | 5 | 17 | 470–694 MHz; 9 kHz–1 MHz; 21.75–22.25 MHz; 9 kHz–6.2 GHz; active retune to 590–600 MHz |
| Controls | 11 | 25 | Gain limits, half-dB quantization, partial settings updates, readback, manual/auto RBW, max-hold, min-hold and average |
| Lifecycle | 7 | 12 | Competing-owner rejection, three stop/start cycles and two close/reopen cycles |
| Physical USB recovery | 5 | 6 | Live unplug, device error, healthy plugin with no analyzer, same-serial rediscovery and fresh acquisition |
| Endurance | 1 | 11041 | One uninterrupted 30-minute 470–694 MHz acquisition session |

Each observed trace had a new sweep ID, the accepted frequency axis and point
count, finite dBm values and a timestamp. Cached traces were excluded when
settings changed. The only expected failure events in the short suites came
from the deliberate competing-owner attempts and USB unplug.

## Endurance results

The acquisition interval lasted **1800.108 seconds** and supplied **11041 fresh
traces**. There were no device failure events or missing-sweep warning events.
The longest observed interval between fresh traces was **0.894 seconds**,
including initial acquisition. This is client-observed timing, not a claim of
continuous RF coverage or a guaranteed refresh rate.

Across 56 resource samples, plugin resident memory ranged from **189.9 to
194.9 MiB** and native-worker memory from **17.4 to 18.1 MiB**. The test exited
normally and no candidate plugin or acquisition worker remained afterwards.
Warning events are not an exact count of discarded acquisitions because the
SDK deduplicates warnings.

## Desktop observations

The candidate appeared in the device picker by serial number. Desktop rendered
the Mac RF attenuation and IF gain controls, accepted the scan configuration,
and displayed an updating real spectrum trace and waterfall. The acquisition
range was 470–694 MHz; the independent plot viewport was approximately
470–616 MHz. Reopening the test project restored its candidate selection and
resumed live data.

Normal Desktop exit removed both the identified plugin and native-worker
processes. This process check does not independently prove receipt of the
worker's cooperative-stop acknowledgement.

Disabling the candidate in Desktop removed its plugin and worker processes.
Re-enabling it and starting Live produced a fresh working scan. After testing,
the older installed plugin was re-enabled, the test project's original analyzer
selection and 470–694 MHz settings were restored, and Desktop was relaunched
normally without the temporary candidate path. It was left on the projects
screen with no scan running. No production project was edited.

On this Mac, prefixing `open -a` with an environment assignment did not make
Desktop discover the candidate. The successful launch was:

```sh
open -a "SoundBase Desktop" --env "SB_PLUGIN_DIRS=/absolute/path/to/plugin-parent"
```

The README and user manual now use that explicit launch method. No security
protections were disabled, and the candidate was not installed through Lab.

## Forced Desktop crash

The test forcibly terminated only the verified Desktop host while the candidate
was acquiring. After three seconds, the plugin and worker were still running.
**Automatic cleanup after a hard host crash did not pass.**

Sending SIGTERM to the identified orphaned plugin cleaned up its owned worker.
A fresh probe then identified the same analyzer and loaded factory correction
successfully, without a USB reconnect. The recovery succeeded, but it does not
remove the core 1.4 host-lifetime limitation. Users must follow the recovery
instructions if Desktop is force-killed.

Two Desktop helper processes also survived the controlled host crash. They were
identified and stopped individually before the final normal app launch.

## Evidence and remaining qualification

Detailed JSON reports, process observations and Desktop screenshots are retained
locally under `.evidence/mac-acceptance-20261026/`; the acceptance client is under
`.evidence/mac-acceptance-tools/`. These include machine-specific information and
are excluded from public source and release assets.

This historical ZIP was not fully qualified for release. Subsequent generator
evidence and native Windows/Mac tests are recorded in the linked reports; those
platform tests used a different CI artifact. The final candidate still needs
the [remaining release checks](rsa306b-validation.md#remaining-release-checks),
including Lab installation. The user manual and launch-instruction updates are
newer than this CI package; including them in a rebuilt ZIP creates a new
candidate identity.
