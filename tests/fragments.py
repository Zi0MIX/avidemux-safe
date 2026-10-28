"""Exercise MP4 fragments above 4 GiB without allocating or copying 4 GiB."""
from pathlib import Path
import argparse
import ctypes
import json
import msvcrt
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import check_avidemux
from smoke import run, frames, check_mp4


def stored_frames(ffmpeg, path):
    # Native 2.8.1 MP4 exports can end the edit list at the last packet's PTS,
    # excluding that picture from normal playback. Check every stored picture
    # here; this regression concerns reader offsets, not native edit-list timing.
    result = run([ffmpeg, "-v", "error", "-xerror", "-ignore_editlist", "1",
                  "-i", str(path), "-map", "0:v:0", "-fps_mode", "passthrough",
                  "-f", "framemd5", "-"])
    if result.stderr.strip():
        raise RuntimeError(result.stderr)
    return [line.split(",")[-1].strip() for line in result.stdout.splitlines()
            if line and not line.startswith("#")]


def make_sparse_fixture(source, destination):
    """Insert a 2**32-byte unreferenced mdat ahead of relative-base moofs."""
    data = source.read_bytes()
    pos, header, fragments = 0, bytearray(), bytearray()
    while pos + 8 <= len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        if size < 8 or size > len(data) - pos:
            raise RuntimeError("Invalid short fixture")
        if kind in (b"ftyp", b"moov"):
            header.extend(data[pos:pos + size])
        elif kind != b"mfra":
            # mfra contains absolute tfra offsets. Omit this optional seek index.
            fragments.extend(data[pos:pos + size])
        pos += size
    if pos != len(data) or not header or not fragments:
        raise RuntimeError("Incomplete short fixture")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.DeviceIoControl.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                      ctypes.c_void_p, ctypes.c_ulong,
                                      ctypes.c_void_p, ctypes.c_ulong,
                                      ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p]
    with destination.open("xb") as stream:
        returned = ctypes.c_ulong()
        if not kernel.DeviceIoControl(msvcrt.get_osfhandle(stream.fileno()),
                                      0x900C4, None, 0, None, 0,
                                      ctypes.byref(returned), None):
            raise ctypes.WinError(ctypes.get_last_error())
        stream.write(header)
        # This reader supports extended-size mdat, but not extended-size free.
        stream.write(struct.pack(">I4sQ", 1, b"mdat", 2**32))
        stream.seek(len(header) + 2**32)
        stream.write(fragments)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--avidemux-dir", required=True, type=Path)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--work-dir", required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("This check needs Windows and a volume supporting sparse files.")
    ctypes.windll.kernel32.SetErrorMode(3)
    app = check_avidemux(args.avidemux_dir)
    work = args.work_dir.resolve()
    if work.exists():
        parser.error("Choose a new work directory.")
    work.mkdir(parents=True)
    small, large = work / "small.mp4", work / "above-4gib.mp4"
    run([args.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc2=s=32x32:r=10",
         "-t", "0.6", "-c:v", "libx264", "-g", "2", "-bf", "0", "-movflags",
         "+frag_keyframe+empty_moov+default_base_moof", str(small)])
    make_sparse_fixture(small, large)
    expected = frames(args.ffmpeg, small)
    if frames(args.ffmpeg, large) != expected:
        raise RuntimeError("The shifted fixture differs before Avidemux.")
    reports = []
    quote = lambda path: json.dumps(path.as_posix(), ensure_ascii=True)
    for name in ("avidemux_cli_portable.exe", "avidemux_portable.exe"):
        output = work / (Path(name).stem + ".mp4")
        script = work / (Path(name).stem + ".py")
        script.write_text("\n".join([
            "#PY", "adm=Avidemux()", "ed=Editor()",
            "adm.loadVideo(" + quote(small) + ")",
            "adm.appendVideo(" + quote(large) + ")",
            "adm.markerA=0", "adm.markerB=ed.getVideoDuration()",
            'adm.videoCodec("Copy")', "adm.audioClearTracks()",
            'adm.setContainer("SafeMP4","muxerType=0","optimize=1",'
            '"forceAspectRatio=False","aspectRatio=1","displayWidth=32",'
            '"rotation=0","clockfreq=0")',
            "adm.save(" + quote(output) + ")"
        ]) + "\n", encoding="utf-8")
        command = [str(app / name), "--nogui", "--run", str(script), "--quit"]
        if name == "avidemux_cli_portable.exe":
            result = run(command, work / (Path(name).stem + ".log"))
            log = result.stdout + result.stderr
        else:
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            subprocess.run(command, check=True, timeout=120, startupinfo=startup,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            log = (app / "settings/admlog.txt").read_text(errors="replace")
            (work / (Path(name).stem + ".log")).write_text(log, encoding="utf-8")
        if "MP4 64-bit fragment positions enabled" not in log or "failed=0" not in log:
            raise RuntimeError("The reader fix or export did not succeed.")
        actual = stored_frames(args.ffmpeg, output)
        if actual != expected + expected:
            raise RuntimeError(f"Decoded frames differ for {name}.")
        reports.append({"executable": name, "frames": len(actual),
                        "all_pixels_match": True, "mp4": check_mp4(output)})
    report = {"fixture_logical_bytes": large.stat().st_size,
              "offset_shift": 2**32, "source_frames": len(expected),
              "output_decode_ignores_native_edit_list": True, "exports": reports}
    (work / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
