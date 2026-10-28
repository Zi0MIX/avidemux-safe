# Validation

The original evidence records describe the v0.1.0 DLL. Later regression
checks are recorded separately below and in evidence.

## Original tests

The local source samples were created from five YouTube downloads and two
Twitch downloads. Each sample was about 12 seconds. Footage and personal
source filenames are deliberately not part of this repository.

The saved records cover:

- Seven-source append, all 5,047 decoded frames checked against source pixels.
- Reopening and resaving through the ordinary Avidemux MP4 muxer.
- Repeated decoder seeks across source configurations.
- Six selections from a trimmed and reordered timeline, checked against
  separately corrected reference exports. The first sample in this set is
  Twitch, followed by YouTube, Twitch, and YouTube.
- Exact AAC packet bytes and timestamps for those selections.
- Three consecutive Save calls with changed markers in one process.
- A selection beyond 72 minutes on an 80-minute repeated-source timeline.
- GUI and CLI executable export paths, including private default selection.
- A 40-minute, 200-section repeated-source benchmark, three runs per mode.

The benchmark retained 144,200 video packets and 103,100 audio packets.
Aggregate video-slice and AAC hashes matched the ordinary export. Median wall
times of 1.232 and 1.220 seconds were within timing noise.

Full decoding of the original approximately 33-hour recording was not done.
YouTube ingestion was not tested.

## Repeat a smoke check

The included smoke check needs the setup portable app, an FFmpeg executable,
and short matching H.264 MP4 source samples from both downloaders. It needs
no additional Python packages.

    python tests/smoke.py --avidemux-dir .\portable --ffmpeg "C:\Tools\ffmpeg.exe" --youtube "E:\samples\youtube.mp4" --twitch "E:\samples\twitch.mp4"

Use samples that begin on IDR pictures and have matching dimensions and frame
rate. Sample extraction is a one-time test step, not part of editing:

    ffmpeg -ss 60 -i youtube-download.mp4 -t 12 -map 0:v:0 -map 0:a:0 -c copy youtube.mp4
    ffmpeg -ss 60 -i twitch-download.mp4 -t 12 -map 0:v:0 -map 0:a:0 -c copy twitch.mp4

The test explicitly loads the portable defaults into the CLI, appends Twitch,
YouTube, and Twitch, then uses the same editor session to save the combined
timeline and all three individual
sections with changed markers. It compares every decoded video frame with
the corresponding sources, decodes audio, and checks avc3 and the front index.
It also reopens and resaves the combined output with the ordinary muxer and
compares its decoded frames.

The smoke check does not reproduce the original exact AAC packet/timestamp
comparison or the long-timeline benchmark. Their saved results are evidence,
not a claim that this smaller check covers every original experiment.

The part comparison permits one picture of marker-endpoint rounding while
requiring every retained picture to match the contiguous source reference.

Decoding in this test intentionally takes time. It is verification only and
does not belong in the normal export workflow.

## Recovery-point regression

Version 0.1.1 accepts Avidemux-validated non-IDR recovery-point keyframes.
The original plugin rejected the first packet of such a cut with only
`packets=1 injected_IDRs=0 failed=1`, even though ordinary MP4 and MKV could
save it. The failure occurred before any media was written, independently
of the selection's length or output size.

The packet-level regression invokes the actual C hook. It covers recovery
points at the start and at a source transition, IDRs, existing in-band
headers, unchanged non-keyframes, and explicit packet errors. Assertions
remain enabled in optimized builds.

    zig cc -target x86_64-windows-gnu -std=c11 -O2 tests/packets.c -o build/test_packets.exe
    .\build\test_packets.exe

The integration regression needs a caller-supplied H.264 source and the
microsecond timestamp of a non-IDR recovery-point keyframe. It compares
every decoded frame with native MP4 Copy output, checks avc3 and the front
index, and reopens/resaves the result. It verifies that the chosen start
really uses a non-IDR keyframe. FFprobe must be beside FFmpeg.

    python tests/recovery.py --avidemux-dir .\portable-built --ffmpeg "C:\Tools\ffmpeg.exe" --source "E:\source.mp4" --start-us 2895699999 --work-dir .\test-output\recovery

The recorded 0.1.1 checks compare all 719 decoded frames of a recovery-point
cut with native MP4 output, including reopen/resave and the GUI executable.
The append smoke check compares 2,163 decoded frames, four consecutive saves,
and audio decoding. A real 11h55m selection completed with 2,574,000 video
packets and an avc3 front index. All 600 decoded frames in a window spanning
its source transition match the sources. Audio was disabled in that saved
project. Full-length validation checks compressed picture packets; it does
not decode the entire 11h55m selection. The SHA-256 of every exported
packet's length-prefixed picture slices matches the corresponding source
packet, across all 2,574,000 packets.

See [the regression evidence](../evidence/recovery-point-verification.json).

## Fragment positions above 4 GiB

Version 0.1.2 corrects one truncated fragment-base argument in the exact
supported reader. The real failing chunk opens with 862,581 frames, and a
second large chunk opens with 825,480 frames. GUI-executable append and two
12-second exports pass, with all 720 decoded frames from each selection
matching the sources beyond 4 GiB. The 2,163-frame append/export smoke check,
audio decoding, consecutive saves and reopen/resave also pass.

The bounded regression `tests/fragments.py` generates a sparse MP4 and checks
all stored pictures after shifting fragments by exactly 2**32 bytes. It uses
both executables. `tests/reader_fix.c` executes the old and corrected machine
instructions and verifies signature and fingerprint refusal. No real footage
is needed by these tests.

See [the reader investigation and limits](fragmented-mp4.md) and
[the saved results](../evidence/fragmented-mp4-verification.json). These tests
do not verify or replace the full zcutter collection.
