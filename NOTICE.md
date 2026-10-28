# Provenance

The plugin adapts the interfaces of Avidemux 2.8.1, an upstream GPL project:

https://github.com/mean00/avidemux2

Reference snapshots were downloaded from the upstream 2.8.1 tag. The original
files are kept under docs/abi-reference, with their notices intact:

- IEditor.h, from avidemux/common/ADM_editor/include/IEditor.h.
- ADM_Video.h, from avidemux_core/ADM_coreDemuxer/include/ADM_Video.h.
- ADM_muxer.h, from avidemux_core/ADM_coreMuxer/include/ADM_muxer.h.
- ADM_muxerInternal.h, from avidemux_core/ADM_coreMuxer/include/ADM_muxerInternal.h.
- ADM_bitstream.h, from avidemux_core/ADM_coreUtils/include/ADM_bitstream.h.
- ADM_mp4Analyzer.cpp, from avidemux_plugins/ADM_demuxers/Mp4/ADM_mp4Analyzer.cpp.
- ADM_atom.h, from avidemux_plugins/ADM_demuxers/Mp4/ADM_atom.h.
- ADM_mp4Fragments.cpp, from avidemux_plugins/ADM_demuxers/Mp4/ADM_mp4Fragments.cpp.

The source locations are also recorded in docs/abi-reference/UPSTREAM.json.
These snapshots support review of the version-locked bridge. The build does
not include or compile them.

The dependency hashes identify an existing Avidemux 2.8.1.22260 MSVC x64
installation. Neither the Avidemux application nor its bundled third-party
libraries are redistributed in this repository.

The compiler used was Zig 0.15.2, available from https://ziglang.org/download/.
Zig itself is an external build dependency and is not bundled.

This package includes no downloaded footage or personal Avidemux preferences.
