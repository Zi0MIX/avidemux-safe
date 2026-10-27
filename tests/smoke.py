"""Integration check with short caller-supplied samples; no Python packages."""
from pathlib import Path
import argparse
import ctypes
import json
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from common import check_avidemux


def run(command, log=None):
    result = subprocess.run(command, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=180)
    if log:
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"Command failed: {command}\n{result.stderr[-2000:]}")
    return result


def frames(ffmpeg, path):
    result = run([ffmpeg, "-hide_banner", "-v", "error", "-xerror", "-i", str(path),
                  "-map", "0:v:0", "-fps_mode", "passthrough", "-f", "framemd5", "-"])
    if result.stderr.strip():
        raise RuntimeError(f"Video decode errors in {path}:\n{result.stderr}")
    hashes = [line.split(",")[-1].strip() for line in result.stdout.splitlines()
              if line and not line.startswith("#")]
    if not hashes:
        raise RuntimeError(f"No decoded frames: {path}")
    return hashes


def audio_decode(ffmpeg, path):
    result = run([ffmpeg, "-hide_banner", "-v", "error", "-xerror", "-i", str(path),
                  "-map", "0:a:0", "-f", "null", "-"])
    if result.stderr.strip():
        raise RuntimeError(f"Audio decode errors in {path}:\n{result.stderr}")


def check_mp4(path):
    """Walk box headers; skip mdat rather than reading the media."""
    found = {}
    with path.open("rb") as stream:
        def walk(start, end, depth=0):
            if depth > 8:
                raise RuntimeError("MP4 nesting too deep")
            pos = start
            while pos + 8 <= end:
                stream.seek(pos)
                size, kind = struct.unpack(">I4s", stream.read(8))
                header = 8
                if size == 1:
                    size = struct.unpack(">Q", stream.read(8))[0]
                    header = 16
                if not size:
                    size = end - pos
                if size < header or size > end - pos:
                    raise RuntimeError("Invalid MP4 box size")
                if depth == 0:
                    found[kind.decode("ascii", errors="replace")] = pos
                if kind == b"stsd":
                    data = stream.read(16)
                    if len(data) == 16 and data[12:16] in (b"avc1", b"avc3"):
                        found["video_entries"] = struct.unpack(">I", data[4:8])[0]
                        found["video_tag"] = data[12:16].decode("ascii")
                elif kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl"):
                    walk(pos + header, pos + size, depth + 1)
                pos += size
        walk(0, path.stat().st_size)
    if found.get("video_tag") != "avc3" or found.get("video_entries") != 1:
        raise RuntimeError(f"Missing single avc3 video description: {found}")
    if not found["moov"] < found["mdat"]:
        raise RuntimeError(f"Missing front index: {found}")
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--avidemux-dir", type=Path, default=ROOT / "portable")
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--youtube", required=True, type=Path)
    parser.add_argument("--twitch", required=True, type=Path)
    parser.add_argument("--work-dir", type=Path, default=ROOT / "test-output")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("This smoke check requires Windows.")
    ctypes.windll.kernel32.SetErrorMode(3)
    app = check_avidemux(args.avidemux_dir)
    exe = app / "avidemux_cli_portable.exe"
    youtube, twitch = args.youtube.resolve(), args.twitch.resolve()
    for path in (exe, youtube, twitch):
        if not path.is_file():
            parser.error(f"Missing file: {path}")
    work = args.work_dir.resolve()
    if work.exists():
        parser.error("Work directory already exists. Choose a new directory.")
    work.mkdir(parents=True)
    quote = lambda path: json.dumps(path.as_posix(), ensure_ascii=True)
    combined = work / "combined.mp4"
    parts = [work / f"part-{i}.mp4" for i in range(3)]
    script = work / "export.py"
    lines = ["#PY", "adm=Avidemux()", "ed=Editor()",
             "adm.loadVideo(" + quote(twitch) + ")",
             "adm.appendVideo(" + quote(youtube) + ")",
             "adm.appendVideo(" + quote(twitch) + ")",
             "d0=ed.getDurationForSegment(0)",
             "d1=ed.getDurationForSegment(1)",
             "d2=ed.getDurationForSegment(2)",
             "adm.markerA=0", "adm.markerB=ed.getVideoDuration()",
             "adm.save(" + quote(combined) + ")"]
    for output, start, end in zip(parts, ("0", "d0", "d0+d1"),
                                  ("d0", "d0+d1", "d0+d1+d2")):
        lines += ["adm.markerA=" + start, "adm.markerB=" + end,
                  "adm.save(" + quote(output) + ")"]
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    # The GUI loads defaultSettings.py automatically; the CLI needs --run.
    result = run([str(exe), "--nogui", "--run", str(app / "settings/defaultSettings.py"),
                  "--run", str(script), "--quit"],
                 work / "export.log")
    if result.stdout.count("[SafeMP4] packets=") != 4 or "failed=1" in result.stdout:
        raise RuntimeError("Expected four successful SafeMP4 saves. See export.log.")
    source_frames = [frames(args.ffmpeg, twitch), frames(args.ffmpeg, youtube)]
    expected = source_frames[0] + source_frames[1] + source_frames[0]
    actual = frames(args.ffmpeg, combined)
    if actual != expected:
        raise RuntimeError(f"Combined frame mismatch: got {len(actual)}, expected {len(expected)}")
    reports = []
    starts = (0, len(source_frames[0]), len(source_frames[0]) + len(source_frames[1]))
    for output, start in zip(parts, starts):
        actual_part = frames(args.ffmpeg, output)
        # Avidemux Copy marker rounding can omit or retain one endpoint picture.
        # Every retained frame must still equal the contiguous full reference.
        if actual_part != expected[start:start + len(actual_part)]:
            raise RuntimeError(f"Part frame mismatch: {output.name}")
        expected_length = len(source_frames[1] if start == starts[1] else source_frames[0])
        if abs(len(actual_part) - expected_length) > 1:
            raise RuntimeError(f"Unexpected part length: {output.name}, {len(actual_part)}")
        audio_decode(args.ffmpeg, output)
        reports.append({"part": output.name, "frames": len(actual_part),
                        "pixels_match": True, "mp4": check_mp4(output)})
    audio_decode(args.ffmpeg, combined)
    combined_info = check_mp4(combined)
    resaved = work / "resaved.mp4"
    run([str(exe), "--nogui", "--load", str(combined), "--output-format", "MP4",
         "--save", str(resaved), "--quit"], work / "resave.log")
    if frames(args.ffmpeg, resaved) != expected:
        raise RuntimeError("Reopened/resaved pixels differ")
    audio_decode(args.ffmpeg, resaved)
    report = {"combined_frames": len(expected), "all_pixels_match": True,
              "consecutive_saves": 4, "reopen_resave_pixels_match": True,
              "combined_mp4": combined_info, "parts": reports}
    (work / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
