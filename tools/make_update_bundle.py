#!/usr/bin/env python3
import os
import struct
import sys
from pathlib import Path

if len(sys.argv) != 4:
    print("Usage: python make_update_bundle.py <output.bin> <firmware.bin> <web_bundle.bin>")
    sys.exit(1)

output_path = Path(sys.argv[1])
firmware_path = Path(sys.argv[2])
web_bundle_path = Path(sys.argv[3])

if not firmware_path.is_file():
    print(f"Missing firmware image: {firmware_path}")
    sys.exit(1)

if not web_bundle_path.is_file():
    print(f"Missing web bundle: {web_bundle_path}")
    sys.exit(1)

firmware_data = firmware_path.read_bytes()
web_bundle_data = web_bundle_path.read_bytes()

body = b'ACN2' + struct.pack('<I', 1) + struct.pack('<I', len(firmware_data)) + struct.pack('<I', len(web_bundle_data)) + firmware_data + web_bundle_data
output_path.write_bytes(body)
print(f"Wrote {output_path} ({len(body)} bytes)")
