"""Shared paths and exact-build checks. Python standard library only."""
from pathlib import Path
import hashlib
import json
import os

ROOT = Path(__file__).resolve().parent
FINGERPRINTS = ROOT / "compatibility" / "binary-fingerprints.json"


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def default_avidemux():
    return Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Avidemux 2.8 VC++ 64bits"


def check_avidemux(directory):
    directory = Path(directory).resolve()
    expected = json.loads(FINGERPRINTS.read_text(encoding="utf-8"))
    for relative, digest in expected.items():
        candidate = directory / relative
        if not candidate.is_file() or sha256(candidate) != digest:
            raise RuntimeError(
                f"Incompatible Avidemux file: {candidate}\n"
                "This experimental adapter requires the exact inspected "
                "Avidemux 2.8.1 MSVC x64 build. See compatibility/binary-fingerprints.json."
            )
    return directory
