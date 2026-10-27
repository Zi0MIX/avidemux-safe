"""Create a fresh portable copy from the exact supported Avidemux installation."""
from pathlib import Path
import argparse
import os
import shutil

from common import ROOT, check_avidemux, default_avidemux, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=default_avidemux())
    parser.add_argument("--destination", type=Path, default=ROOT / "portable")
    parser.add_argument("--plugin", type=Path, default=ROOT / "binaries/libADM_mx_safe_mp4.dll",
                        help="Use build/libADM_mx_safe_mp4.dll to test a fresh build")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This POC requires Windows x64.")
    source = check_avidemux(args.source)
    destination = args.destination.resolve()
    if destination == source or source in destination.parents or destination in source.parents:
        parser.error("Source and destination must be separate directory trees.")
    if destination.exists():
        parser.error("Destination already exists. Choose a new empty destination.")
    plugin = args.plugin.resolve()
    if not plugin.is_file():
        parser.error(f"Plugin does not exist: {plugin}")
    if plugin == (ROOT / "binaries/libADM_mx_safe_mp4.dll").resolve():
        expected = (ROOT / "binaries/SHA256SUMS").read_text().split()[0]
        if sha256(plugin) != expected:
            parser.error("The bundled plugin failed its checksum.")
    # Never import the user's preferences, project history, or recovery scripts.
    ignored = shutil.ignore_patterns(
        "Uninstall*", "InstallationLog.txt", "components.xml", "network.xml",
        "settings", "avidemux_portable.exe", "avidemux_cli_portable.exe",
        "safe-plan.json",
    )
    shutil.copytree(source, destination, ignore=ignored)
    shutil.copy2(plugin, destination / "plugins/muxers/libADM_mx_safe_mp4.dll")
    shutil.copytree(ROOT / "settings", destination / "settings")
    for name in ("avidemux", "avidemux_cli"):
        shutil.copy2(destination / (name + ".exe"), destination / (name + "_portable.exe"))
    print("Portable copy created:", destination / "avidemux_portable.exe")
    print("Use Copy for video/audio and MP4 with source headers (POC) for output.")


if __name__ == "__main__":
    main()
