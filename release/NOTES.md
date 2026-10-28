# Tektronix RSA306B 1.0.0

The first stable release brings the RSA306B into SoundBase Desktop's live
spectrum plot and waterfall on Apple silicon Macs and Windows x64.

Setup is automatic: the native workers and Windows fallback Python runtime
are bundled. The optional Windows API library is detected in standard
installation locations, so no path configuration is needed.

- Native USB acquisition from 9 kHz to 6.2 GHz with the analyzer's factory
  calibration, adjustable RF attenuation and IF gain, and SoundBase trace modes.
- Bundled macOS arm64 and Windows x64 workers. No separate Node.js, Python,
  Homebrew or compiler installation is required.
- Windows needs the Tektronix RSA USB driver supplied by SignalVu-PC or the
  RSA API installer. Keep **Windows acquisition backend** on **Native USB**
  for normal use; the optional **Tektronix API** fallback needs the x64 API.
- Discovered analyzers supply their serial number automatically; the serial
  field is shown only when adding a device manually.

Download **rsa306b-1.0.0.zip** below; GitHub's automatic source archives are not
installable plugin packages. See the [user manual](https://github.com/kjungk/rsa306b-plugin/blob/v1.0.0/docs/user-manual.md)
for setup and direct package loading. The plugin is also published on
[SoundBase Lab](https://lab.soundbase.app); check the version listed there.

Requires macOS 14 or later on Apple silicon, or Windows x64, USB 3 SuperSpeed,
and an RSA306B with HW7, firmware 1.7 and FPGA 2.1 for native acquisition. Use one
analyzer and one acquisition application at a time. Close SignalVu before
scanning; it can remain installed and be used after the plugin releases the device.

Sweeps acquire frequency segments sequentially. Intel Macs, Windows arm64 and
other native hardware revisions are not supported. Qualification summaries and
the ZIP checksum accompany the download.
