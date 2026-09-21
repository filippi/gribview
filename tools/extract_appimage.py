#!/usr/bin/env python3
"""Extract a type-2 ELF64 AppImage without executing its runtime (also on Rosetta)."""
import argparse
from pathlib import Path
import struct
import subprocess


def extract(source, destination):
    data = source.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("Expected a little-endian ELF64 AppImage")
    offset = struct.unpack_from("<Q", data, 40)[0]
    entry_size, count = struct.unpack_from("<HH", data, 58)
    end = offset + entry_size * count
    for index in range(count):
        section = offset + index * entry_size
        section_type = struct.unpack_from("<I", data, section + 4)[0]
        file_offset, size = struct.unpack_from("<QQ", data, section + 24)
        if section_type != 8:  # SHT_NOBITS has no bytes in the file.
            end = max(end, file_offset + size)
    if data[end:end + 4] != b"hsqs":
        raise ValueError("No SquashFS image at the end of the ELF runtime")
    subprocess.run(["unsquashfs", "-o", str(end), "-d", str(destination), str(source)], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    extract(args.source, args.destination)
