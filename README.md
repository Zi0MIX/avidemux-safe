# Avidemux MP4 source-header plugin

Experimental Windows x64 plugin for lossless append, cut, and part exports
from mixed 4K Video Downloader and Twitch Leecher DX H.264 downloads.

The tested files have matching visible video properties but different H.264
decoder settings. Ordinary Avidemux Copy exports can retain the wrong settings
after a source transition. This plugin inserts each source's SPS/PPS headers
at IDR pictures during the normal export.

It keeps compressed picture slices and audio unchanged. There is no encoding,
intermediate footage, preparation pass, or repair pass.

## Use the included DLL

Requirements:

- Windows x64.
- The exact Avidemux 2.8.1.22260 MSVC x64 build described in
  [compatibility/binary-fingerprints.json](compatibility/binary-fingerprints.json).
- Python 3.10 or later to create the portable folder. Python is not needed
  when editing or saving in that folder.

From a PowerShell terminal in this repository:

    python setup_portable.py
    .\portable\avidemux_portable.exe

The setup tool copies the installed application into a fresh, separate
directory and adds the plugin. It checks the exact dependency hashes first.
It creates clean portable settings, with automatic update checks disabled.
It does not change the installed app or copy your personal settings.

For a different installation location:

    python setup_portable.py --source "E:\Apps\Avidemux 2.8 VC++ 64bits"

In the portable app:

1. Load, append, and cut the footage as usual.
2. Keep Video Output and Audio Output on Copy.
3. Select MP4 with source headers (POC) as Output Format.
4. Set A/B markers and use normal Save for each part.

The portable settings select that output format by default. Loading a saved
project can override it, so check the dropdown afterwards. Normal keyframe
restrictions for Copy cuts still apply. The core may still show its existing
codec-setting compatibility warning.
The GUI loads `defaultSettings.py` automatically; CLI scripts must load it
explicitly with `--run`.

## Build from source

Install [Zig 0.15.2](https://ziglang.org/download/), the compiler used for the
tested DLL. Its archive URL and checksum are preserved in
[compatibility/build-toolchain.json](compatibility/build-toolchain.json).
Then run:

    python build.py --zig "C:\Tools\zig-x86_64-windows-0.15.2\zig.exe"
    python setup_portable.py --plugin .\build\libADM_mx_safe_mp4.dll --destination .\portable-built

No Avidemux SDK, full Avidemux rebuild, FFmpeg installation, or Python packages
are required to compile or set up the plugin. The only C source is
[src/safe_mp4.c](src/safe_mp4.c).

The included DLL is the tested binary. Its checksum is in
[binaries/SHA256SUMS](binaries/SHA256SUMS). A compiler rebuild can have a
different binary hash because of PE metadata and build paths.

## Exact Avidemux dependency

The original release is available on the project's
[2.8.1 download page](https://sourceforge.net/projects/avidemux/files/avidemux/2.8.1/).
The Windows installer is named "Avidemux_2.8.1 VC++ 64bits.exe".
The setup tool's installed-file checks are the authority for compatibility.
The full application is an external dependency and is not bundled in this
repository.

Do not install this DLL into another Avidemux build. It relies on the inspected
MSVC object layouts, virtual method slots, and executable wrapper. It is a
working proof of concept rather than a supported Avidemux extension API.

## Verification and limits

The original verification found:

- All 5,047 decoded frames from seven source samples matched the sources.
- Six selected part exports matched corrected references, including their
  AAC packets and audio timestamps.
- Reopening and resaving, seeking in both directions, consecutive saves with
  changed A/B markers, and a selection beyond 72 minutes passed.
- A 40-minute, 200-section Copy benchmark measured medians of 1.232 seconds
  for ordinary MP4 and 1.220 seconds for this plugin. The difference is timing
  noise. No material slowdown was demonstrated.

Packaging checks also exercised both the included DLL and a fresh build in
isolated portable applications. All 2,163 combined decoded frames matched
the sources; three part saves, audio decoding, and reopen/resave checks passed.

The plugin added 12,000 bytes in that benchmark. It retained all compressed
picture slices and AAC packets. These measurements used short repeated
samples, not a full 40-minute recording read from disk.

YouTube upload processing remains untested. This addresses the demonstrated
local corruption. It is not a general solution for changing resolution,
codec, frame structure, or arbitrary H.264 streams.

See [docs/validation.md](docs/validation.md) and
[evidence](evidence) for the saved results. An optional repeatable smoke check
is in [tests/smoke.py](tests/smoke.py). Footage is not included.

## Repository contents

- src: the native plugin source.
- binaries: the tested DLL and its checksum.
- build.py and setup_portable.py: standalone build and isolated setup tools.
- settings: clean portable defaults.
- compatibility: exact supported original binary fingerprints.
- docs: implementation notes and upstream ABI reference snapshots.
- evidence: original verification records with machine-specific paths removed.
- tests: a smoke check using caller-supplied source samples.

Generated applications, test outputs, and build files are ignored.

## License and attribution

The imported package is provided under GPL-2.0-or-later. See
[LICENSE.GPL-2.0](LICENSE.GPL-2.0). The repository's original
[LICENSE](LICENSE) contains GPL-3.0, an allowed later version.
The ABI reference snapshots retain the upstream Avidemux notices.
See [NOTICE.md](NOTICE.md) for source provenance. This is an independent
experiment and is not an official Avidemux release.
