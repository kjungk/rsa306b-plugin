# Native Windows USB investigation on TPX1

This is the **historical 26 October feasibility report**. Native acquisition,
reconnect, endurance and subsequent SignalVu handoff were implemented and tested
on 27 October; see [the native Windows validation report](windows-native-validation-2026-10-27.md)
and [current status](implementation-status.md). The proposed work below records
what remained at the time of this descriptor-only investigation.

Investigated on **26 October 2026**. Direct USB access through TPX1's existing
RSA306B driver succeeded without loading the Tektronix RSA API. The official
API acquired three fresh spectra before the probe and three afterwards, with
no driver replacement or USB reconnect. The user then confirmed that SignalVu
connected to the analyzer and displayed a live spectrum.

The strongest route for preserving SignalVu is a native Windows worker that
uses this existing driver. This would remove the RSA API runtime dependency,
but a clean Windows computer would still need a compatible USB driver installed.
At the time of this investigation, native spectrum acquisition through this
transport remained to be implemented and tested.

## Installed driver

| Item | Observation |
| --- | --- |
| Host | TPX1, Windows 11 Pro x64, build 26300 |
| Analyzer | RSA306B, HW7, firmware 1.7, FPGA 2.1 |
| USB identity | `0699:a306` |
| Driver service and file | `RSA3USB3`, `RSA3USB3.sys` |
| File identity | Cypress Generic USB Device Driver-WDF; original filename `CyUsb3.sys` |
| SYS and INF version | `1.2.3.20`, INF dated 8 November 2017 |
| Installed INF | `oem48.inf` |
| Device interface GUID | `{85C528EE-C558-4C31-9277-A7F5DCF958DC}` |
| Existing official API | `5.8.0275.0`, supplied with SignalVu-PC |

Authenticode reported valid signatures for the SYS file and the driver package's
Tektronix catalog. The device was healthy with problem code 0, and the memory
integrity registry setting was enabled. The WMI `IsSigned` field separately
reported false; it was not used as the signature verdict. These observations
describe this installed host, not qualification of a new driver package.

## Direct access and handoff results

The isolated probe used Windows SetupAPI and `DeviceIoControl`, with the
documented Cypress interface and request structures. It loaded neither
`RSA_API.dll` nor CyAPI. Its operations were limited to the driver-version query
and standard device/configuration descriptor reads. It used asynchronous I/O,
bounded waits and request-specific cancellation.

| Check | Result |
| --- | --- |
| Official API baseline | Three complete 590–600 MHz spectra, 1001 finite dBm values each |
| Native driver interface | Exactly one matching present device opened successfully |
| Driver-version IOCTL | Succeeded; value treated as an interface check rather than the installed version |
| USB device descriptor | Expected VID/PID and USB 3.0 descriptor |
| USB configuration | Configuration 1, interface 0, alternate 0 |
| Command endpoints | Interrupt OUT `0x03` and interrupt IN `0x83` |
| Data endpoint | Bulk IN `0x84`; endpoints match the Mac backend's protocol |
| Handle cleanup | Native probe closed its device handle |
| Official API after native access | Three more complete spectra, without reconnecting USB |
| State after automated checks | Original service/INF and SYS hash unchanged; healthy device; no test workers remaining |
| SignalVu application check | User confirmed a live spectrum after the probe and API checks |

The descriptor probe does not establish interrupt-command exchange, sustained
bulk streaming, calibration or native RF acquisition on Windows. The before/after
spectra use the official API and establish that this small native probe did not
disrupt that acquisition path.

The SignalVu application result is a user-observed live-spectrum check. Together
with the automated API checks, it confirms successful return to the existing
software after this descriptor probe. Handoff after full native acquisition
was still pending at that point and subsequently passed in the linked
27 October implementation tests.

The Cypress interface is documented in the
[CyUSB3 Programmer's Reference](https://community.infineon.com/gfawx74859/attachments/gfawx74859/cnusbsuperspeed/1249/1/CyUSB.pdf).
Constants and packed structures were checked against the
[official USB Suite source archive](https://www.infineon.com/assets/row/public/documents/30/416/infineon-cyusb3-usb-suite-source-software-code-example-en.zip).
The driver-version response can differ from the installed SYS version, as
[Infineon documents](https://community.infineon.com/t5/USB-superspeed-peripherals/Driver-version-digits-how-to-interpret/td-p/36408).

## Driver choices and SignalVu compatibility

| Approach | Setup requirement | SignalVu implication |
| --- | --- | --- |
| Native worker using the existing Cypress-compatible RSA driver | No new driver on TPX1; install a compatible signed driver on a clean PC | Preserves the existing binding; the direct probe and subsequent official-API acquisition passed |
| Native worker using libusb and WinUSB | Associate the analyzer with WinUSB | Replaces the current function-driver binding; SignalVu compatibility is not established |
| libusb with UsbDk | Install a USB filter driver | Can temporarily redirect the device, but adds a driver and is not the preferred route |

Windows has one function driver per device stack. Standard libusb does not expose
the Cypress driver's interface directly; its Windows documentation describes
WinUSB, libusbK/libusb-win32 and UsbDk options and discourages UsbDk because of
stability concerns. [Microsoft device stacks](https://learn.microsoft.com/en-us/windows-hardware/drivers/gettingstarted/device-nodes-and-device-stacks),
[libusb Windows documentation](https://github.com/libusb/libusb/wiki/Windows).

Preserving SignalVu means allowing the applications to use the analyzer in turn.
It does not mean simultaneous acquisition. A native worker must release its USB
handle and leave the instrument in a suitable stopped state before SignalVu
connects. Cypress can allow multiple application handles, so an internal plugin
mutex alone is not proof of exclusion against SignalVu.
[Infineon on multiple handles](https://community.infineon.com/t5/USB-superspeed-peripherals/Opening-multiple-handles-for-the-same-FX3-device/td-p/278208).

## What can be bundled

The plugin can contain our native executable and its permitted runtime
dependencies. The proposed direct transport would not need Tektronix's RSA API
DLLs, SignalVu's installation, or the CyAPI library to acquire data. That was
the implementation target, not an acquisition result from this descriptor probe;
the later validation report records the completed native backend.

A machine without the USB driver still needs a one-time driver installation.
Placing driver files in a plugin ZIP does not register them with Windows.
Distributing a driver installer together with the plugin requires confirming
redistribution rights for the exact package and retaining valid driver signing.
An independently packaged Cypress driver would also need the correct hardware
IDs, interface registration and separate SignalVu compatibility testing.

## Implementation steps proposed on 26 October

1. Separate the current native protocol/acquisition code from its libusb
   transport. Keep the Mac implementation and add a Windows transport using the
   documented Cypress control, interrupt and asynchronous bulk-transfer interface.
2. Port cancellation, ownership locking and the native build/package scripts to
   Windows x64. Define how the native backend's gain controls are presented
   alongside the existing vendor-API backend's reference-level control.
3. Validate protocol identity, factory correction, LF/RF acquisition, USB
   recovery, cancellation and a long-running scan through the native transport.
4. Test native acquisition followed by SignalVu acquisition, and the reverse,
   without driver rebinding. Check busy-device handling against SignalVu itself.
5. Test a clean Windows installation and settle the signed-driver distribution
   route before advertising an all-in-one installer.

Raw host observations, diagnostic scripts, source hashes and acquisition reports
are retained locally under `.evidence/windows-native-feasibility-20261026/`.
Probe files on TPX1 are isolated in a dedicated investigation directory. No
driver, security setting, installed plugin or application package was changed.
