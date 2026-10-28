# Implementation

## Why the tested exports corrupted

The source recordings were H.264 High, 1280x720, approximately 60 fps, with
AAC-LC stereo at 44.1 kHz. The visible properties matched, but the SPS/PPS
configurations did not. They reused parameter-set ID 0 with different
reference-frame and picture-order settings.

The Twitch samples included in-band SPS/PPS at IDRs. The YouTube samples did
not. After the decoder switched to Twitch settings, a later YouTube section
could not restore its settings. Keeping just the first global AVC header
therefore did not work for the combined timeline.

## Native adapter

The DLL delegates container configuration and writing to the existing
libADM_mx_mp4.dll. It wraps the copy video stream's getPacket virtual method.
It gets the live editor's retained segments, source demuxers, and A/B markers
at the start of every export. No script-generated plan or file scan is needed.

For each output keyframe, it selects the source section using the presentation
timestamp and inserts that source's length-prefixed SPS/PPS if the same header
prefix is not already present. It does not change VCL picture NAL units or
audio packets. Ordinary packets take the fast path.

Avidemux also marks non-IDR recovery-point I pictures as keyframes. The
adapter accepts these after the core's normal Copy-cut checks and inserts
SPS/PPS before them. Requiring an IDR at the first packet incorrectly rejected
valid recovery-point cuts before writing any frames. Packet failures now log
their reason, timestamp, flags, size and capacity. The GUI also shows the
reason in a dialog; CLI and `--nogui` exports only log it.

After the native muxer closes, the adapter walks MP4 box headers and changes
the AVC sample-entry tag from avc1 to avc3. It changes four bytes. It does not
move the index or copy the footage again. Native front-index optimization
remains available.

## Version dependency

The C adapter recreates the relevant MSVC x64 layouts and virtual calls.
It resolves the editor pointer from the inspected executable's getExtraData
wrapper. The executable PE timestamp and image size are checked at runtime.
The setup tool checks SHA-256 fingerprints for both executables, the MP4
muxer and demuxer, and ADM_coreMuxer6.dll.

The ABI reference snapshots in docs/abi-reference explain the slots used by
the bridge. They are documentation, not build dependencies.

The single active export assumption follows this Avidemux save path.
There is no support for concurrent exports in one process or loading the DLL
directly into a newer Avidemux version.

## Old reader diagnostic bug

A separate Avidemux 2.8.1 debug-dump bug assumes a single SPS in avcC.
The Twitch files contain duplicate SPS entries. The debug code can interpret
the second SPS as a PPS length and read outside the allocation when dumping it.

This diagnostic workaround replaces only the loaded MP4 reader's mixDump import with a
no-op callback in the portable process. It pins the callback DLL for the
process lifetime. This suppresses the unsafe diagnostic without changing
source data or demuxing. The original DLL on disk is unchanged.

See the upstream 2.8.1 ADM_mp4Analyzer.cpp snapshot, near lines 1217-1228.

## Fragment positions above 4 GiB

Version 0.1.2 also corrects the pinned reader's `parseMoof` call to `parseTraf`.
The inlined `adm_atom::getStartPos()` narrows an `int64_t` atom position to
`uint32_t`. The caller consequently passes a wrapped fragment base after
4 GiB, despite `parseTraf` and the packet index accepting 64-bit positions.

At reader RVA `0x84c6`, changing the REX prefix from `44` to `4c` widens
`mov r8d, [rbx+8]` to `mov r8, [rbx+8]`. Instruction length, control flow,
stack layout and downstream data structures stay the same. The helper checks
the loaded reader's on-disk SHA-256, PE timestamp, image size, instruction
context and callee signature before writing. It restores page protection and
flushes the instruction cache. A mismatch refuses the correction and logs
that it was not applied.

This happens during muxer registration, before the supported GUI and CLI
load footage. It does not depend on selecting the SafeMP4 output format.
It is a correction to the exact reader in process, not a general demuxer
replacement. See [the detailed investigation](fragmented-mp4.md).

## Future upstream implementation

A maintained implementation should add per-source parameter-set injection to
the core Copy exporter using supported C++ interfaces. That would remove the
binary wrapper lookup, recreated object layouts, and version lock.
