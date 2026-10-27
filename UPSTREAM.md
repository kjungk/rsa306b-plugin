# Source and dependency provenance

The plugin started from `soundbase-lab/soundbase-plugin-template`, lineage
**1.0.15**, generated from SoundBase commit
`7f3cd7f23533d5c769f70e8d17eb91dcc4a43a07`. That lineage remains unchanged in the
manifest and package metadata. The template bootstrap and tooling retain the
license in `LICENSE-TEMPLATE` even where adapted.

The original RSA306B workers, adapters and selected tests were imported from
Kevin Jungk's `tektronix-ui` source at commit
`20c7bc63c3612e1a2380b998efaf56b62429bb19` (2026-10-26). The source project's
license scope explicitly covers its original plugin, native acquisition and
release code under MIT. Additional original numerical-validation helpers and
tests imported and adapted for this implementation are covered by this project's
MIT license; the former source scope did not enumerate every such helper. The
Swift application and unrelated private research
are not part of this import. The original repository is left unchanged.

The two SoundBase packages are installed from **public npm**, exactly version
0.12.2. `package-lock.json` records their integrity digests and the resolved Zod
version. No dependency is taken from a local SoundBase app bundle. This matters:
the app-bundled 0.12.2 observed during planning contains a core 1.5 host lifeline,
but the public package exports core 1.4 and lacks that feature.

libusb and Windows embeddable Python versions, URLs and SHA-256 pins are recorded
under `release/`. Their licenses and required corresponding-source material are
included with generated runtimes. Build archives are verified before extraction.
The Windows native transport was added in this repository. It uses SetupAPI and
the existing RSA3USB3/Cypress-compatible driver interface; it links no Tektronix
or Cypress userspace library. The compatible USB driver remains a user-installed
Windows prerequisite. Only the explicitly selected API fallback additionally
requires the installed x64 Tektronix RSA API. Calibration
correction reads the attached unit's factory tables; no user's factory table
bytes or private capture archive belongs in a public release.

Numerical comparisons against private reference captures may produce local
reports in `.evidence/`. Public tests must use synthetic fixtures or small
explicitly shareable protocol fixtures. A numerical/reference pass is not
independent signal-generator validation or qualification of every RSA306B unit.
