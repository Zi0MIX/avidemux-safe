# Validation

The evidence records describe the original tested DLL, not an untested
reimplementation. The source in src/safe_mp4.c is preserved verbatim.

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
