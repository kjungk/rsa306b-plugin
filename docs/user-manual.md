# Tektronix RSA306B plugin user manual

For version **1.0.0** · Updated **28 October 2026**

The Tektronix RSA306B plugin brings live spectrum measurements into SoundBase
Desktop. Select your analyzer in a Coord project's live-scan settings, choose
the frequency range, and use SoundBase's plot and trace controls. The plugin
has no separate application window.

The plugin is published on SoundBase Lab. Check the listed version before
installing; GitHub may have a newer release. The native backend uses the
analyzer's factory calibration.

## Contents

- [Requirements](#requirements)
- [Install and enable the plugin](#install-and-enable-the-plugin)
- [Start your first scan](#start-your-first-scan)
- [Adjust the scan](#adjust-the-scan)
- [Understand the measurements](#understand-the-measurements)
- [Plugin and device settings](#plugin-and-device-settings)
- [Use SignalVu on the same Windows computer](#use-signalvu-on-the-same-windows-computer)
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
| Analyzer | RSA306B hardware revision 7, firmware 1.7, FPGA 2.1 | Native: the same HW7 / 1.7 / 2.1 combination; API fallback: RSA306B supported by the installed API |
| Acquisition software | Included in the complete plugin package | Included in the complete plugin package; install the compatible Tektronix RSA USB driver separately. |
| USB connection | USB 3 SuperSpeed | USB 3 SuperSpeed for Native USB |

Use one connected RSA306B and one live scan at a time. Intel Macs, Windows arm64,
Linux and multiple attached analyzers are outside this version's support.
The native backend on both platforms refuses other device revisions.
Compatible Windows software includes `RSA3USB3.sys` version 1.2.3.20 and, for
the optional API backend, x64 RSA API 5.8.0275.0. Other driver and API versions
may need additional compatibility checks.

You do not need to install Node.js, Python, Homebrew or a compiler to use a
complete plugin package. SoundBase and the Tektronix installer may have their
own operating-system requirements. Hardware acquisition runs in Desktop on the
computer connected to the analyzer; a browser alone cannot host it.

Use the supplied USB cable or a suitable USB 3 replacement. Connect an antenna
or measurement cable appropriate for your frequency range, and follow the
instrument's connection and input-limit instructions in the
[Tektronix installation and safety manual](https://www.tek.com/en/support/datasheets-manuals-software-downloads?series=RSA306B+USB+Spectrum+Analyzer&type=manual).

## Install and enable the plugin

### Mac preparation

Use the Apple-silicon version of SoundBase Desktop and a complete plugin
package. The package includes the native worker and libusb; no Tektronix
software, separate USB driver, Python or Homebrew installation is needed.
Connect the RSA306B through USB 3 and close other analyzer applications before
starting the scan. Intel Macs are not supported.

### Windows preparation

1. Install the **Tektronix RSA USB driver** on the Windows computer connected
   to the analyzer. If SignalVu-PC already works with the RSA306B, the driver
   is already installed; keep it and close SignalVu-PC. Without SignalVu-PC,
   install Tektronix's **RSA API/USB-driver package** to obtain the driver.
   Tektronix documents both options in its
   [USB-driver FAQ](https://www.tek.com/en/support/faqs/where-do-i-get-usb-driver-rsa306);
   software is available from the
   [RSA306B downloads page](https://www.tek.com/en/support/datasheets-manuals-software-downloads?series=RSA306B+USB+Spectrum+Analyzer).
   The tested driver is `RSA3USB3.sys` version 1.2.3.20. Do not replace it with
   WinUSB or use Zadig.
2. Connect the RSA306B and allow driver installation to finish.
3. Keep **Windows acquisition backend** on **Native USB (default)**.

The plugin automatically uses its bundled acquisition software. No worker path,
Python installation or interpreter path is needed. The USB driver is the Windows
system component that lets this software communicate with the analyzer; it is
required on every Windows computer, not just a particular test machine.

Installing the RSA API package to obtain the driver does **not** require
switching to the API backend. Native USB uses the driver directly and does not
load `RSA_API.dll`. SignalVu-PC does not need to be installed or running.

**Tektronix API (fallback)** is an optional alternative for compatibility or
troubleshooting. Only that backend needs the x64 RSA API library as well as the
USB driver. Its Python runtime is included and selected automatically. See
[Switch the Windows backend](#switch-the-windows-backend) if you need it.

### Installation through Lab

Install the approved version from Lab using the steps below. To load a newer
GitHub release directly, use its complete ZIP and follow
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
   supplies its identity automatically. The **Analyzer serial number** field
   appears only when adding a device manually.
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
| Points | Number of values supplied for a displayed trace; requested range 2–2000. | Start with 1001. More points do not by themselves improve the analyzer's RBW. The native backend may reduce the count to match the available measured bins. |
| Trace mode | SoundBase can display live, max-hold, min-hold and average traces. | Max-hold retains observed peaks; average smooths changing readings. Hold and average values include earlier accepted sweeps. |

The native backend on either platform uses discrete RBW values from
approximately 160 Hz to 1.31 MHz. The Windows API fallback accepts 1 kHz–1 MHz.
The selected span also limits RBW, and very wide native scans may require a coarser value to fit
a complete sweep. **Auto does not mean a fixed 10 kHz**, and an explicitly
requested value may be adjusted. Use the actual value reported by SoundBase
when comparing measurements.

### Native controls (macOS and Windows)

| Control | Range and step | Initial value |
| --- | --- | --- |
| RF attenuation | 0–31 dB in 0.5 dB steps | 0.5 dB |
| IF gain | −11 to +20 dB in 0.5 dB steps | 11 dB |

More RF attenuation reduces the signal entering the RF acquisition path and can
help with strong signals. Less IF gain can help avoid overload later in that
path. Change one control at a time and check the warnings and resulting trace.
These controls affect acquisition; they are not offsets to add to a reading.

**Below 22 MHz, the native backend uses a separate path with fixed gain.** The RF
attenuation and IF gain settings do not change its gain. Sweeps crossing 22 MHz
switch paths automatically. If an LF measurement overloads, changing these RF
controls will not solve it; reduce the signal reaching the input using an
appropriate measurement setup.

### Windows API fallback control

| Control | Range and step | Initial value |
| --- | --- | --- |
| Reference level | −50 to +20 dBm in 1 dB steps | −20 dBm |

Use the analyzer's **Reference level** device control to adjust the Tektronix
API acquisition level. For strong signals or overload, try a higher, more
positive reference level and check the next complete sweep. Changing only the
plot's vertical display limits does not perform this acquisition adjustment.

The API can adjust requested settings. The plugin returns the actual reference
level and RBW to SoundBase. The fallback exposes reference level instead of
the native backend's RF attenuation and IF gain controls.

## Understand the measurements

**Each trace is assembled from sequential observations.** Portions of a wide
span are measured at different times, with gaps between acquisitions. A short
transmission can occur between observations and be missed. A full-range scan
does not provide simultaneous coverage from 9 kHz to 6.2 GHz, and a waterfall
does not remove those gaps. Narrow the span when you need more frequent updates
in a particular band.

Levels are reported in **dBm**. The native backend on both platforms applies
the attached unit's factory correction tables. The Windows API fallback uses
the official Tektronix API conversion. The
plugin does not apply your antenna factor, cable loss or external amplifier
gain. These readings are not automatically field strength or transmitter
output power.

Only complete, valid sweeps are submitted to SoundBase. Missing measurements,
overload and other quality flags can cause a pass to be withheld. If that
happens, **the previous trace may remain visible**. Read the warning and confirm
that new sweeps arrive before interpreting the display as current RF activity.
Hold modes intentionally retain older measurements as well.

For comparisons, keep the frequency range, actual RBW, gain/reference level,
antenna and cable setup consistent. Account for external losses and check the
complete measurement setup before relying on an absolute level.

## Plugin and device settings

For normal use, **keep the defaults**. The plugin selects its bundled worker
for your operating system automatically. No executable or Python paths need
to be configured.

Open **Settings → Plugins → Tektronix RSA306B** for the single installation
setting: **Windows acquisition backend**. macOS ignores this setting and always
uses its bundled native worker. Scan frequencies and gain controls are
configured separately in the live-scan dialog.

- **Native USB (default):** use this for normal operation. On Windows it needs
  the Tektronix RSA USB driver, supplied with SignalVu-PC or the RSA API installer.
  It does not load the Tektronix API library.
- **Tektronix API (fallback):** an optional alternative that uses the installed
  x64 RSA API library and the same USB driver. The plugin finds the API in its
  standard installation locations and uses its bundled Python interpreter.

There are no worker, Python or API path fields. Install Tektronix software using
its standard location so the API fallback can find it automatically.

Select your analyzer from the discovered-device list. Its **serial number** is
supplied automatically; the serial-number input is only shown when adding a
device manually. The plugin checks the serial when connecting and will not
silently substitute another unit.

### Switch the Windows backend

1. Stop **Live** and close the analyzer's live-scan connection.
2. In the plugin settings, choose **Native USB (default)** or
   **Tektronix API (fallback)**. For the fallback, confirm the x64 API is
   installed in its standard location; the plugin finds it automatically.
3. Wait for the plugin's prerequisite check to finish, then select the analyzer
   again in the live-scan settings.
4. Check the device controls before restarting. Native USB uses **RF attenuation**
   and **IF gain**; the API fallback uses **Reference level**. Review the accepted
   scan settings rather than assuming values transfer between backends.

The plugin does not automatically switch to the API if Native USB fails. The
selected backend and its setup or acquisition error remain visible.

## Use SignalVu on the same Windows computer

Both applications can remain installed and use the same Tektronix RSA USB driver
one at a time. They must not acquire simultaneously: the driver allows multiple
handles, and the plugin's ownership check cannot prevent SignalVu from accessing
an active session.

To switch from SoundBase to SignalVu:

1. Stop **Live**, close the analyzer connection and quit SoundBase normally.
2. Wait for acquisition to stop, then open SignalVu and connect to the analyzer.
3. If SignalVu reports the analyzer busy, use the
   [recovery steps](#stop-scanning-and-recover-the-analyzer) before retrying.

To return to SoundBase, stop acquisition and close SignalVu completely. Start
SoundBase, select the RSA306B and begin a fresh scan. No driver rebinding or
routine USB reconnect is part of the normal handoff.

## Troubleshooting

Start in **Settings → Plugins → Tektronix RSA306B** and read its status and log.
The acquisition prerequisite check retries about every 30 seconds after a setup
failure, and runs again after you change plugin settings.

| Symptom | What to check or do |
| --- | --- |
| Plugin does not appear | Use SoundBase Desktop on a supported architecture. Check that the package is installed and enabled. For an evaluation ZIP, check the folder layout and launch instructions below. |
| Needs setup on Windows | Reinstall the complete plugin package if its bundled runtime is missing. For the API fallback, install or repair the x64 RSA API in its standard location. |
| Native Windows discovery cannot access USB | Install the compatible Tektronix RSA USB driver through SignalVu-PC or the RSA API installer, and close other analyzer software. Do not replace the Tektronix driver with WinUSB. |
| Needs setup on Mac | Use the complete arm64 package on macOS 14 or later. Reinstall the complete package if its bundled worker is missing. No path setting is needed. |
| Tektronix API not found | Install or repair the official x64 RSA API using its standard location. If you intended to use the bundled native worker, select **Native USB (default)** instead. |
| Plugin is ready but no analyzer is listed | Connect exactly one RSA306B, check USB 3 connectivity, and close competing applications. Discovery can take several seconds to refresh. A ready plugin does not mean a device is attached. |
| Unsupported device revision with Native USB | Check hardware, firmware and FPGA versions against the requirements above. Other combinations need maintainer support. |
| Already open, busy or access error | Close the other live-scan connection and SignalVu-PC or other acquisition software. If this followed a crash, use the recovery steps below. |
| SignalVu no longer connects after a plugin scan | Quit SoundBase normally and confirm its analyzer connection has closed. If a process remains after a crash, follow the recovery sequence. Keep the Tektronix RSA USB driver installed. |
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

The former worker, Python and API path settings have been removed. Previously
saved values are ignored; bundled runtimes and automatic API detection are used.
No manual cleanup of old settings is needed.

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
needs its own USB driver installation. The native controls match between
platforms. If switching to the Windows API fallback, review reference level:
it is not interchangeable with native RF attenuation and IF gain.

## Get help

Report reproducible problems in the
[GitHub issue tracker](https://github.com/kjungk/rsa306b-plugin/issues), including:

- Plugin version, SoundBase Desktop version, OS version and processor type.
- Selected Windows backend and USB driver version, when using Windows.
- Analyzer model and hardware/firmware/FPGA versions, if known; Windows API
  version when relevant.
- Frequency range, actual RBW, point count, platform controls and trace mode.
- The exact error or warning, relevant plugin log lines, and steps to reproduce.
- Whether another application can acquire the analyzer after SoundBase closes.

Remove private project details from shared logs. For evaluation packages,
include the ZIP filename and supplied checksum so the maintainer can identify
the supplied build. This is a community plugin; Tektronix provides the instrument
and its Windows API, while plugin integration issues belong in this repository.

## Load a supplied evaluation package

This is a local testing method for a complete package supplied by the maintainer.
It does not install or approve a Lab release. GitHub's automatically generated
**Source code** archives are not runnable plugin packages.

If the maintainer supplies a GitHub Actions artifact named **plugin-zip**, first
extract that outer artifact. Use the inner `rsa306b-1.0.0.zip` and its `.sha256`
file (substitute the supplied version). Extracting the outer artifact alone
does not create a runnable plugin folder. Compare the inner ZIP's SHA-256 with
the supplied checksum before loading it:

```sh
# macOS; adjust the path to the supplied inner ZIP
shasum -a 256 "$HOME/Downloads/rsa306b-1.0.0.zip"
```

```powershell
# Windows; adjust the path to the supplied inner ZIP
Get-FileHash -Algorithm SHA256 "$env:USERPROFILE\Downloads\rsa306b-1.0.0.zip"
```

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
