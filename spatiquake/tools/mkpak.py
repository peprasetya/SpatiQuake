#!/usr/bin/env python3
"""Write a Quake .pak from a directory: mkpak.py OUT.pak DIR [DIR ...]

Each file under a DIR is stored by its path relative to that DIR. Used for test data
(LibreQuake's loose 2D files, before its maps are built), not for releases."""
import os
import struct
import sys


def main():
    out, dirs = sys.argv[1], sys.argv[2:]
    entries = []
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in sorted(files):
                p = os.path.join(root, f)
                name = os.path.relpath(p, d).replace(os.sep, "/")
                if len(name) < 56:
                    entries.append((name, p))
    with open(out, "wb") as o:
        o.write(b"\0" * 12)
        table = []
        for name, p in entries:
            data = open(p, "rb").read()
            table.append((name, o.tell(), len(data)))
            o.write(data)
        offset = o.tell()
        for name, pos, size in table:
            o.write(struct.pack("<56sii", name.encode(), pos, size))
        o.seek(0)
        o.write(struct.pack("<4sii", b"PACK", offset, len(table) * 64))
    print(f"{out}: {len(table)} files")


if __name__ == "__main__":
    main()
