# Tektronix RSA306B plugin user manual

For plugin version **0.1.0** · Updated **26 October 2026**

The Tektronix RSA306B plugin brings live spectrum measurements into SoundBase
Desktop. Select your analyzer in a Coord project's live-scan settings, choose
the frequency range, and use SoundBase's plot and trace controls. The plugin
has no separate application window.

**Current status: development candidate.** This version has passed automated
checks, Mac functional and endurance tests, and a Windows acquisition test with
an RSA306B. Those hardware results cover different candidate packages;
qualification of one final release and independent signal-generator measurements
remain pending. It is not yet an approved Lab release. Use it for evaluation;
do not treat its readings as independently verified amplitude measurements.

## Contents

- [Requirements](#requirements)
- [Install and enable the plugin](#install-and-enable-the-plugin)
- [Start your first scan](#start-your-first-scan)
- [Adjust the scan](#adjust-the-scan)
- [Understand the measurements](#understand-the-measurements)
- [Plugin and device settings](#plugin-and-device-settings)
- [Troubleshooting](#troubleshooting)
- [Stop scanning and recover the analyzer](#stop-scanning-and-recover-the-analyzer)
- [Updates and existing projects](#updates-and-existing-projects)
- [Get help](#get-help)
- [Load a supplied evaluation package](#load-a-supplied-evaluation-package)

## Requirements

| Requirement | macOS | Windows |
| --- | --- | --- |
| Computer | Apple silicon Mac, macOS 14 or later | Windows x64 on an Intel or AMD processor |
| SoundBase | SoundBase Desktop for Apple silicon with plugin support | SoundBase Desktop for Windows x64 with plugin support |
| Analyzer | RSA306B hardware revision 7, firmware 1.7, FPGA 2.1 | RSA306B supported by the installed Tektronix API |
| Acquisition software | Included in the complete plugin package | Official Tektronix x64 RSA API and compatible USB driver, installed separately |
| USB connection | USB 3 SuperSpeed | USB 3 connection supported by the Tektronix driver |

Use one connected RSA306B and one live scan at a time. Intel Macs, Windows arm64,
Linux and multiple attached analyzers are outside this version's support.
The Mac backend refuses device revisions other than the combination listed above.
The Windows acquisition test used API version 5.8.0275.0; that is a tested version,
not a claim that every other API version is compatible.

You do not need to install Node.js, Python, Homebrew or a compiler to use a
complete plugin package. SoundBase and the Tektronix installer may have their
own operating-system requirements. Hardware acquisition runs in Desktop on the
computer connected to the analyzer; a browser alone cannot host it.

Use the supplied USB cable or a suitable USB 3 replacement. Connect an antenna
or measurement cable appropriate for your frequency range, and follow the
instrument's connection and input-limit instructions in the
[Tektronix installation and safety manual](https://www.tek.com/en/support/datasheets-manuals-software-downloads?series=RSA306B+USB+Spectrum+Analyzer&type=manual).

## Install and enable the plugin

### Windows preparation

1. Get the **64-bit RSA API** and USB driver from
   [Tektronix downloads](https://www.tek.com/en/support/datasheets-manuals-software-downloads?series=RSA306B+USB+Spectrum+Analyzer).
   Tektronix also supplies the USB driver with SignalVu-PC. Follow the vendor's
   installer instructions; these components are not included in the plugin.
2. Connect the RSA306B and allow driver installation to finish. If you use
   SignalVu-PC to check the connection, close it completely afterwards.
3. Leave **Tektronix API path (Windows)** empty initially. The plugin searches
   standard RSA API and SignalVu-PC installation locations. If it reports
   **Needs setup**, see [Plugin and device settings](#plugin-and-device-settings).

macOS uses the acquisition software bundled with the plugin and does not require
the Windows Tektronix API.

### Installation through Lab

These steps apply **once an approved release is available**. The current
development candidate is not yet listed as an approved release. If the maintainer
has supplied a complete evaluation ZIP, use
[Load a supplied evaluation package](#load-a-supplied-evaluation-package).

1. Open [SoundBase Lab](https://lab.soundbase.app), sign in if requested, and find
   **Tektronix RSA306B**. Confirm that the listing points to
   [kjungk/rsa306b-plugin](https://github.com/kjungk/rsa306b-plugin).
2. Follow the listing's installation flow into SoundBase Desktop.
3. In Desktop, open **Settings → Plugins**, find **Tektronix RSA306B**, and enable it.
4. Check its status. A ready plugin may have no analyzer attached. **Needs setup**
   means an acquisition prerequisite needs attention; open the status details.

The evaluation package uses ad-hoc signing on macOS and an unsigned Windows
worker. If your computer's security policy blocks it, retain the message and
contact the maintainer or your administrator. Do not remove system protections
to make the package run.

## Start your first scan

1. Connect exactly one RSA306B. Close SignalVu-PC, the older RSA306B plugins and
   any other software using the analyzer.
2. Open your project in **Coord**, select the zone you want to measure, and open
   its plot.
3. Click the gear in the live-scan control group, labelled
   **Open Live Scan Data Settings**.
4. In **Select a device**, choose **Tektronix RSA306B** followed by your unit's
   serial number. Match it to your instrument. Selecting a discovered device
   supplies its identity; you do not need to type its serial into the optional
   **Analyzer serial number** field.
5. Choose a frequency range appropriate for the antenna and signals you want to
   observe. For a first UHF scan, **470–694 MHz**, **Auto RBW** and **1001 points**
   are a useful example. Check the unit shown beside each frequency field.
6. Leave the platform controls at their defaults initially and save the settings.
   If **Live** is not already running, press it to start acquisition.
7. Allow a complete sweep to finish. Confirm that the trace updates and check
   any device warnings. A wide scan takes longer than a narrow scan.

Device assignment belongs to the selected zone. This plugin can open only one
scan at a time, so close its current device connection before assigning the same
analyzer to another active scan.

## Adjust the scan

SoundBase presents the scan settings in the live-scan dialog. Existing project
settings can override the plugin's initial values. After changing a value, check
the accepted value: the plugin adjusts requests to the settings it can acquire.
Changing settings during a scan briefly interrupts acquisition while the new
configuration is applied.

The plot's visible frequency range and the analyzer's scan range can differ.
Enable **Link parameter settings to frequency plot** if you want the scan to
follow the plot. With it disabled, changing the plot view does not change the
saved acquisition range. Keep SoundBase's **Offset** at 0 dBm when checking the
plugin's unadjusted readings.

### Common controls

| Control | What it does | How to use it |
| --- | --- | --- |
| Start and stop frequency | Set the measured band within 9 kHz–6.2 GHz. | Scan the band you need. Out-of-range values and spans that are too small are adjusted. |
| Center and span | Another way SoundBase can present the same frequency range. | Check the resulting start and stop frequencies after a change. |
| RBW | Resolution bandwidth determines how closely spaced signals can be resolved. | Start with Auto. Narrower RBW can reveal more frequency detail and usually requires more acquisition work. |
| Points | Number of values supplied for a displayed trace; requested range 2–2000. | Start with 1001. More points do not by themselves improve the analyzer's RBW. macOS may reduce the count to match the available measured bins. |
| Trace mode | SoundBase can display live, max-hold, min-hold and average traces. | Max-hold retains observed peaks; average smooths changing readings. Hold and average values include earlier accepted sweeps. |

Available RBW values differ between platforms. Windows accepts 1 kHz–1 MHz;
macOS uses discrete values from approximately 160 Hz to 1.31 MHz. The selected
span also limits RBW, and very wide Mac scans may require a coarser value to fit
a complete sweep. **Auto does not mean a fixed 10 kHz**, and an explicitly
requested value may be adjusted. Use the actual value reported by SoundBase
when comparing measurements.

### macOS controls

| Control | Range and step | Initial value |
| --- | --- | --- |
| RF attenuation | 0–31 dB in 0.5 dB steps | 0.5 dB |
| IF gain | −11 to +20 dB in 0.5 dB steps | 11 dB |

More RF attenuation reduces the signal entering the RF acquisition path and can
help with strong signals. Less IF gain can help avoid overload later in that
path. Change one control at a time and check the warnings and resulting trace.
These controls affect acquisition; they are not offsets to add to a reading.

**Below 22 MHz, the Mac backend uses a separate path with fixed gain.** The RF
attenuation and IF gain settings do not change its gain. Sweeps crossing 22 MHz
switch paths automatically. If an LF measurement overloads, changing these RF
controls will not solve it; reduce the signal reaching the input using an
appropriate measurement setup.

### Windows control

| Control | Range and step | Initial value |
| --- | --- | --- |
| Reference level | −50 to +20 dBm in 1 dB steps | −20 dBm |

Use the analyzer's **Reference level** device control to adjust the Tektronix
API acquisition level. For strong signals or overload, try a higher, more
positive reference level and check the next complete sweep. Changing only the
plot's vertical display limits does not perform this acquisition adjustment.

The API can adjust requested settings. The plugin returns the actual reference
level and RBW to SoundBase. Mac RF attenuation and IF gain settings have no
equivalent controls in this Windows implementation.

## Understand the measurements

**Each trace is assembled from sequential observations.** Portions of a wide
span are measured at different times, with gaps between acquisitions. A short
transmission can occur between observations and be missed. A full-range scan
does not provide simultaneous coverage from 9 kHz to 6.2 GHz, and a waterfall
does not remove those gaps. Narrow the span when you need more frequent updates
in a particular band.

Levels are reported in **dBm**. macOS applies the attached unit's factory
correction tables; Windows uses the official Tektronix API conversion. The
plugin does not apply your antenna factor, cable loss or external amplifier
gain. These readings are not automatically field strength or transmitter
output power.

Only complete, valid sweeps are submitted to SoundBase. Missing measurements,
overload and other quality flags can cause a pass to be withheld. If that
happens, **the previous trace may remain visible**. Read the warning and confirm
that new sweeps arrive before interpreting the display as current RF activity.
Hold modes intentionally retain older measurements as well.

For comparisons, keep the frequency range, actual RBW, gain/reference level,
antenna and cable setup consistent. The current validation scope is recorded in
the [implementation and validation record](implementation-status.md).

## Plugin and device settings

Open **Settings → Plugins → Tektronix RSA306B** for installation settings.
These settings describe software on the local computer; they are separate from
the scan frequency and device controls.

| Setting | Normal use |
| --- | --- |
| Tektronix API path (Windows) | Leave empty for automatic lookup. If needed, enter the full path to the installed `RSA_API.dll` or its containing directory. |
| Native worker path (advanced, macOS) | Leave empty to use the bundled acquisition worker. Change only when the maintainer supplies a compatible replacement. |
| Python interpreter (advanced, Windows) | Leave empty to use the bundled interpreter. Installing a system Python is not a normal setup step. |

For example, if this file exists on your computer, the Windows API path may be
`C:\Tektronix\RSA_API\lib\x64\RSA_API.dll` or
`C:\Tektronix\RSA_API\lib\x64`. Enter the path as plain text, without quotation
marks. All overrides must be full absolute paths. Keep the vendor installation
intact so its dependencies remain available.

**Analyzer serial number** is an optional explicit device setting. The selected
discovered device already identifies its serial, even if Desktop leaves this
field blank. Selecting a different physical unit requires selecting its serial
in the device picker.
The plugin checks the serial when connecting and will not silently substitute
another unit.

## Troubleshooting

Start in **Settings → Plugins → Tektronix RSA306B** and read its status and log.
The acquisition prerequisite check retries about every 30 seconds after a setup
failure, and runs again after you change plugin settings.

| Symptom | What to check or do |
| --- | --- |
| Plugin does not appear | Use SoundBase Desktop on a supported architecture. Check that the package is installed and enabled. For an evaluation ZIP, check the folder layout and launch instructions below. |
| Needs setup on Windows | Install the x64 RSA API and USB driver. Check the API path and ensure it points to the 64-bit DLL. If the bundled interpreter is missing, restore the complete plugin package. |
| Needs setup on Mac | Use the complete arm64 package on macOS 14 or later. Clear an unintended worker-path override and reinstall the complete package if necessary. |
| Invalid configuration or absolute-path error | Clear unused advanced path fields, or replace a relative path with the full path to the intended file or directory. |
| Plugin is ready but no analyzer is listed | Connect exactly one RSA306B, check USB 3 connectivity, and close competing applications. Discovery can take several seconds to refresh. A ready plugin does not mean a device is attached. |
| Unsupported device revision on Mac | Check hardware, firmware and FPGA versions against the requirements above. Other combinations need maintainer support. |
| Already open, busy or access error | Close the other live-scan connection and SignalVu-PC or other acquisition software. If this followed a crash, use the recovery steps below. |
| Selected analyzer differs or serial mismatch | Choose the connected unit again in the device picker; do not reuse another analyzer's saved serial. |
| Trace is slow to update | Reduce the span and try Auto or a coarser RBW. Wait for a complete pass and check for withheld-sweep warnings. |
| Incomplete, quality-flagged or missing-sweep warning | Treat the visible trace as older data. Check USB connectivity, overload and gain/reference settings. If the warning specifically reports unobserved output bins, reduce the point count. |
| Invalid calibration or factory-correction error | Save the error and contact the maintainer. The plugin cannot produce an accepted trace without valid correction data. |
| Timeout or USB disconnect | Stop and close the scan, reconnect the analyzer, then select it again. See the recovery sequence below if it remains busy. |
| Simulation warning | The data are synthetic. Stop the scan and restart Desktop without the evaluation environment's mock settings before measuring RF. |

If an operating-system message blocks execution, include that exact message in
your support request. Do not assume that a missing trace is a quiet RF band.

## Stop scanning and recover the analyzer

For normal use, stop **Live**, then close its device connection or disable the
plugin before handing the analyzer to SignalVu-PC or another application.
Stopping acquisition alone can leave the live-scan connection reserved inside
the plugin. Quit Desktop normally when finished.

After a disconnect or failed acquisition:

1. Stop the scan and close its device connection. Disable the RSA306B plugin if
   the connection cannot close.
2. Close other software that may be using the analyzer.
3. Disconnect and reconnect the USB cable; allow the device to initialize.
4. Enable the plugin, select the analyzer again, and start a fresh scan.

If Desktop was forcibly terminated, an RSA306B plugin or acquisition process can
remain running and hold the device. Quit Desktop, then use **Activity Monitor**
on macOS or **Task Manager** on Windows to stop only processes identified as
belonging to this plugin. A process named Python or Node is not sufficient
identification: check its executable or command-line path. If you cannot identify
it, restart the computer. Reconnect the analyzer and start Desktop again.

## Updates and existing projects

Stop acquisition before updating or replacing a plugin package. For Lab
releases, use SoundBase's installation/update flow and verify the resulting
version in **Settings → Plugins**. For evaluation packages, quit Desktop and
replace the entire supplied package rather than copying individual files over
an older version.

If you previously used **rsa306b-native** or **rsa306b-windows**, disable that
plugin before enabling this one. The unified plugin is **rsa306b**. Open each
affected project's live-scan settings and select the analyzer from the new
plugin. Existing projects are not migrated automatically.

When moving a project between Mac and Windows, verify the serial and scan
settings again. Each computer needs its own plugin installation; Windows also
needs its own Tektronix API/driver installation. Adjust that platform's controls
because Mac gain values and Windows reference level are not interchangeable.

## Get help

Report reproducible problems in the
[GitHub issue tracker](https://github.com/kjungk/rsa306b-plugin/issues), including:

- Plugin version, SoundBase Desktop version, OS version and processor type.
- Analyzer model and hardware/firmware/FPGA versions, if known; Windows API
  version when relevant.
- Frequency range, actual RBW, point count, platform controls and trace mode.
- The exact error or warning, relevant plugin log lines, and steps to reproduce.
- Whether another application can acquire the analyzer after SoundBase closes.

Remove private project details from shared logs. For evaluation packages,
include the ZIP filename and supplied checksum so the maintainer can identify
the tested build. This is a community plugin; Tektronix provides the instrument
and its Windows API, while plugin integration issues belong in this repository.

## Load a supplied evaluation package

This is a local testing method for a complete package supplied by the maintainer.
It does not install or approve a Lab release. GitHub's automatically generated
**Source code** archives are not runnable plugin packages.

1. Disable any older RSA306B integration, then quit SoundBase Desktop completely.
2. Extract the supplied ZIP into a dedicated folder such as `SoundBasePlugins`
   inside your home folder. Keep the package's `rsa306b` folder intact:

   ```text
   SoundBasePlugins/
     rsa306b/
       soundbase-plugin.json
       main.js
       node_modules/
       runtime/
   ```

3. Start a fresh Desktop process with `SB_PLUGIN_DIRS` set to **SoundBasePlugins**,
   the parent of `rsa306b`. Pointing it at `rsa306b` itself will not find the plugin.
   Use only the intended package in that parent folder.

On macOS, with that folder under your home directory, run in Terminal:

```sh
open -a "SoundBase Desktop" --env "SB_PLUGIN_DIRS=$HOME/SoundBasePlugins"
```

On Windows, run in PowerShell. The executable below is the standard per-user
installation location; substitute your installed Desktop executable if it is
elsewhere:

```powershell
$env:SB_PLUGIN_DIRS = Join-Path $env:USERPROFILE 'SoundBasePlugins'
& "$env:LOCALAPPDATA\SoundBasePro\current\SoundBase Desktop.exe"
```

Open **Settings → Plugins** and continue with
[Start your first scan](#start-your-first-scan). The complete package includes
its runtimes and dependencies; do not run an npm install step for this method.

To return to a Lab installation, quit Desktop and launch it normally without
`SB_PLUGIN_DIRS`. In the PowerShell session above, clear the setting with
`Remove-Item Env:SB_PLUGIN_DIRS` before launching again. Avoid loading an
evaluation copy alongside an installed copy of the same plugin.
