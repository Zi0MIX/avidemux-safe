# Fragmented MP4 positions above 4 GiB

Version 0.1.2 corrects one confirmed loading crash in the pinned Avidemux
2.8.1.22260 MSVC x64 MP4 reader. It applies a one-byte correction to loaded
code. Source recordings, delivered chunks, existing outputs and the original
reader DLL on disk are not modified. No remux or preparation pass is involved.

## Cause and correction

The upstream [atom helper](abi-reference/ADM_atom.h) stores `_atomStart` as
`int64_t` but returns `uint32_t` from `getStartPos()`. The
[fragment parser](abi-reference/ADM_mp4Fragments.cpp) passes that result from
`parseMoof` to `parseTraf` as a 64-bit fragment base. The upper bits have
already been lost. The resulting packet offsets wrap by exactly 2**32 bytes.

In the reported chunk, the first invalid length `0x990da7df` is present at
byte 17,991,781. The correct packet at byte 4,312,959,077 starts with valid
SPS/PPS and picture data. The two locations differ by exactly 4,294,967,296.
An isolated baseline run reproduced CLI exception `0xc0000005`.

The inspected DLL loads this argument with `mov r8d, dword ptr [rbx+8]`
at RVA `0x84c6`. Changing the REX prefix from `44` to `4c` gives
`mov r8, qword ptr [rbx+8]`. The native callee already saves a qword argument,
and fragment positions and packet-index offsets are already 64-bit. No
instruction, index or object needs to grow.

The plugin verifies the reader SHA-256, PE identity, all 17 surrounding
instruction bytes and the callee's 64-bit argument instruction. The details
are in [the compatibility record](../compatibility/mp4-reader-fix.json).
It uses temporary page protection, changes one byte, flushes the instruction
cache and restores protection. A different reader or modified instruction
is refused. The startup log must report:

    [SafeMP4] MP4 64-bit fragment positions enabled (reader RVA 0x84c6).

If it reports `NOT applied`, this loading correction is not active.
The existing duplicate-SPS diagnostic workaround is separate.

## Verification

- Original chunk 11: 5,628,986,152 bytes. The baseline CLI crashed; the corrected
  CLI opened it normally and reported 862,581 frames.
- Original chunk 18: 6,200,761,425 bytes. The corrected CLI opened it normally
  and reported 825,480 frames. Its uncorrected reader was not independently
  tested here.
- The GUI executable, using `--nogui` automation, loaded chunk 11 and appended
  chunk 18. It exported separate selections read from beyond 4 GiB in each.
  All 720 decoded frames in each selection match the corresponding source.
- A sparse synthetic MP4 shifts every fragment by exactly 2**32 bytes using
  an unreferenced extended-size `mdat`. It occupies only a few KB on disk.
  Both GUI and CLI append it to the original small fragment file and export
  all 12 stored pictures with matching decoded pixels.
- Native instruction tests execute the original and corrected loads across
  4 GiB and 8 GiB. They check exact-signature refusal, repeat application,
  restored page protection and SHA-256 refusal for unsupported files.

The sparse test checks all stored pictures with FFmpeg's `-ignore_editlist 1`.
The native 2.8.1 MP4 writer ends this tiny export's edit list at the last
picture's PTS, so default playback omits that final picture. This existing
writer timing behavior is not changed by the reader correction.

Repeat the bounded tests with Zig 0.15.2 and an FFmpeg build with libx264:

    zig cc -target x86_64-windows-gnu -std=c11 -O2 tests/reader_fix.c -o build/test_reader_fix.exe
    .\build\test_reader_fix.exe
    python tests/fragments.py --avidemux-dir .\portable --ffmpeg "C:\Tools\ffmpeg.exe" --work-dir .\test-output\fragments

The tests generate their own short pattern and require a Windows volume
supporting sparse files. They do not use or replace a managed collection.

## Limits

This is a version-locked correction to an external reader, outside the normal
muxer extension API. A maintained upstream fix should change `getStartPos()`
to return the full 64-bit position and rebuild the reader. Other Avidemux
versions must be inspected and tested separately.

Other MP4 reader problems remain outside this correction. For example, this
reader handles extended-size `mdat` but not general extended-size `free`
boxes. Fragment timing, malformed NAL parsing and arbitrary damaged MP4
files are not repaired here. Unknown or fingerprint-mismatched builds are
left untouched and receive no protection from this correction.

The complete zcutter collection has not been loaded, decoded or verified by
this investigation. Successfully loading two files and matching selected
frames is not managed preservation verification. Any replacement or remuxed
collection still needs zcutter's own verification and promotion process.

See [the saved results](../evidence/fragmented-mp4-verification.json).
