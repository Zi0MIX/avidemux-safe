"""Compare a recovery-point Copy cut with native MP4, including reopen/resave."""
from pathlib import Path
import argparse
import ctypes
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import check_avidemux
from smoke import run, frames, check_mp4


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--avidemux-dir", type=Path, required=True)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--start-us", type=int, required=True,
                        help="Timestamp of a source non-IDR recovery-point keyframe")
    parser.add_argument("--duration-us", type=int, default=12000000)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("This check requires Windows.")
    ctypes.windll.kernel32.SetErrorMode(3)
    app = check_avidemux(args.avidemux_dir)
    source = args.source.resolve()
    if not source.is_file():
        parser.error(f"Missing source: {source}")
    work = args.work_dir.resolve()
    if work.exists():
        parser.error("Choose a new work directory.")
    work.mkdir(parents=True)
    quote = lambda p: json.dumps(p.as_posix(), ensure_ascii=True)
    native, fixed, again = [work / name for name in
                            ("native.mp4", "source-headers.mp4", "resaved.mp4")]
    script = work / "export.py"
    lines = ["#PY", "adm=Avidemux()", "ed=Editor()",
             "adm.loadVideo(" + quote(source) + ")",
             "duration=ed.getVideoDuration()", "adm.clearSegments()",
             f"adm.addSegment(0,{args.start_us},duration-{args.start_us})",
             "adm.markerA=0", f"adm.markerB={args.duration_us}",
             'adm.videoCodec("Copy")', "adm.audioClearTracks()"]
    for container, output in (("MP4", native), ("SafeMP4", fixed)):
        lines += [f'adm.setContainer("{container}","muxerType=0","optimize=1",'
                  '"forceAspectRatio=False","aspectRatio=1","displayWidth=1280",'
                  '"rotation=0","clockfreq=0")',
                  "adm.save(" + quote(output) + ")"]
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    exe = str(app / "avidemux_cli_portable.exe")
    result = run([exe, "--nogui", "--run", str(script), "--quit"], work / "export.log")
    if "failed=0" not in result.stdout or "[SafeMP4] ERROR:" in result.stderr:
        raise RuntimeError("Source-header export failed. See export.log.")
    expected, actual = frames(args.ffmpeg, native), frames(args.ffmpeg, fixed)
    if expected != actual:
        raise RuntimeError("Decoded frames differ from native Copy output.")
    # Verify the supplied cut actually exercises the original regression.
    ffprobe = str(Path(args.ffmpeg).with_name("ffprobe.exe"))
    packet = json.loads(run([ffprobe, "-v", "error", "-select_streams", "v:0",
                            "-read_intervals", "%+#1", "-show_packets", "-show_entries",
                            "packet=pos,size,flags", "-of", "json", str(native)]).stdout)["packets"][0]
    with native.open("rb") as stream:
        stream.seek(int(packet["pos"]))
        data = stream.read(int(packet["size"]))
    pos, picture = 0, None
    while pos + 4 <= len(data):
        size = int.from_bytes(data[pos:pos + 4], "big")
        if not size or size > len(data) - pos - 4:
            raise RuntimeError("Invalid native packet.")
        kind = data[pos + 4] & 31
        if 1 <= kind <= 5:
            picture = kind
            break
        pos += 4 + size
    if picture != 1 or "K" not in packet["flags"]:
        raise RuntimeError("Choose a non-IDR keyframe to exercise this regression.")
    info = check_mp4(fixed)
    run([exe, "--nogui", "--load", str(fixed), "--output-format", "MP4",
         "--save", str(again), "--quit"], work / "resave.log")
    if frames(args.ffmpeg, again) != expected:
        raise RuntimeError("Reopened/resaved frames differ.")
    report = {"frames": len(actual), "first_picture_nal_type": picture,
              "pixels_match_native": True, "reopen_resave_pixels_match": True,
              "mp4": info}
    (work / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
