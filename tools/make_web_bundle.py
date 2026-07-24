#!/usr/bin/env python3
import os
import struct
import sys
from pathlib import Path

if len(sys.argv) < 2:
    print("Usage: python make_web_bundle.py <output.bin> [files...]")
    sys.exit(1)

output_path = Path(sys.argv[1])
entries = []
for arg in sys.argv[2:]:
    path = Path(arg)
    if not path.is_file():
        print(f"Missing file: {path}")
        sys.exit(1)
    entries.append(path)

if not entries:
    print("No input files specified")
    sys.exit(1)

payload = bytearray()
for path in entries:
    name = path.name
    data = path.read_bytes()
    payload.extend(struct.pack('<I', len(name)))
    payload.extend(name.encode('utf-8'))
    payload.extend(struct.pack('<I', len(data)))
    payload.extend(data)

body = b'WAB1' + struct.pack('<I', 1) + struct.pack('<I', len(entries)) + payload
output_path.write_bytes(body)
print(f"Wrote {output_path} ({len(body)} bytes)")
