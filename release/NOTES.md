# Tektronix RSA306B 0.1.0 beta

The first public beta brings the RSA306B into SoundBase Desktop's live spectrum
plot and waterfall on Apple silicon Macs and Windows x64.

- Native USB acquisition from 9 kHz to 6.2 GHz with the analyzer's factory
  calibration, adjustable RF attenuation and IF gain, and SoundBase trace modes.
- Bundled macOS arm64 and Windows x64 workers. No separate Node.js, Python,
  Homebrew or compiler installation is required.
- Windows uses the installed Tektronix RSA USB driver. The optional Tektronix
  API backend is available for installations with the x64 RSA API.

Download **rsa306b-0.1.0.zip** below; GitHub's automatic source archives are not
installable plugin packages. See the [user manual](https://github.com/kjungk/rsa306b-plugin/blob/v0.1.0/docs/user-manual.md)
for setup and direct package loading. Lab installation becomes available after
SoundBase approves the listing.

Requires macOS 14 or later on Apple silicon, or Windows x64, USB 3 SuperSpeed,
and an RSA306B with HW7, firmware 1.7 and FPGA 2.1 for native acquisition. Use one
analyzer and one acquisition application at a time. Close SignalVu before
scanning; it can remain installed and be used after the plugin releases the device.

Sweeps acquire frequency segments sequentially. After a forced Desktop crash,
an acquisition process can remain running; the manual describes recovery.
This community beta does not support Intel Macs, Windows arm64 or other native
hardware revisions. Qualification summaries and the ZIP checksum accompany
the download.

During Windows testing, SoundBase Desktop 2.26.0 encountered a black screen
while creating a project. Its HTTP-client assertion also occurred without this
plugin loaded. The existing-project retry passed, including plot, waterfall,
project reopen, plugin enable/disable and normal shutdown. The plugin does not
fix that host issue.
