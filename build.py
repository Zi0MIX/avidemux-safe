"""Compile the native DLL without an Avidemux SDK or third-party Python modules."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

from common import ROOT, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", help="Path to zig.exe; otherwise ZIG or PATH")
    parser.add_argument("--output", type=Path, default=ROOT / "build/libADM_mx_safe_mp4.dll")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("The tested build procedure requires Windows x64.")
    zig = args.zig or os.environ.get("ZIG") or shutil.which("zig")
    if not zig:
        parser.error("Install Zig 0.15.2 and use --zig PATH_TO_ZIG_EXE.")
    version = subprocess.check_output([zig, "version"], text=True).strip()
    if version != "0.15.2":
        parser.error(f"The preserved build uses Zig 0.15.2; found {version}.")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        zig, "cc", "-target", "x86_64-windows-gnu", "-std=c11", "-O2",
        "-shared", str(ROOT / "src/safe_mp4.c"), "-o", str(output),
    ], check=True)
    print(output)
    print("SHA-256:", sha256(output))


if __name__ == "__main__":
    main()
