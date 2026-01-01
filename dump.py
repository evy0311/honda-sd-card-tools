#!/usr/bin/env python3
import sys
import time
import struct
import serial

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <serial_port> <output_img>")
    sys.exit(1)

port = sys.argv[1]
out_path = sys.argv[2]

BAUD = 115200

print(f"Opening {port} @ {BAUD}...")
ser = serial.Serial(port, BAUD, timeout=1)

# Small settle
time.sleep(0.5)
ser.reset_input_buffer()

print("Requesting init + CID from Teensy...")

# 1) Ask Teensy to init and show CID
ser.write(b"i\n")
time.sleep(0.2)
ser.write(b"c\n")
time.sleep(0.2)
ser.flush()

cid_line = None
start = time.time()

# Read some lines for up to ~3s to catch the CID printout
while time.time() - start < 3.0:
    line = ser.readline()
    if not line:
        continue
    text = line.decode(errors="ignore").strip()
    if text:
        print(text)
    if text.startswith("CID:"):
        cid_line = text

if cid_line:
    print(f"[HOST] Detected CID from Teensy: {cid_line}")
else:
    print("[HOST] WARNING: no 'CID:' line seen. Check wiring / password / sketch output.")
    # continue anyway; maybe it printed earlier or is a different format

# 2) Tell Teensy to start full dump.
print("[HOST] Sending 'd' to start dump...")
ser.write(b"d\n")
ser.flush()

print("[HOST] Waiting for STARTIMG header from Teensy...")

# Wait for STARTIMG\n
while True:
    line = ser.readline()
    if not line:
        continue
    text = line.decode(errors="ignore").strip()
    if not text:
        continue
    print(text)
    if text.startswith("STARTIMG"):
        break

# 3) Read 4-byte little-endian block count
bc = ser.read(4)
if len(bc) != 4:
    print("[HOST] Failed to read block count from Teensy.")
    sys.exit(1)

blocks = struct.unpack("<I", bc)[0]
if blocks == 0:
    print("[HOST] Reported block count is 0. Aborting.")
    sys.exit(1)

print(f"[HOST] Card reports {blocks} blocks ({blocks * 512 / (1024*1024):.2f} MiB).")
print(f"[HOST] Writing raw image to: {out_path}")

# 4) Stream all blocks into file
total_bytes = blocks * 512
received = 0

with open(out_path, "wb") as f:
    while received < total_bytes:
        chunk = ser.read(min(4096, total_bytes - received))
        if not chunk:
            print(f"\n[HOST] Read timeout / no data at {received} bytes.")
            break
        f.write(chunk)
        received += len(chunk)
        # progress every ~1 MiB
        if received % (1024 * 1024) < 4096:
            pct = received * 100.0 / total_bytes
            print(f"\r[HOST] {received}/{total_bytes} bytes ({pct:.1f}%)", end="", flush=True)

print("\n[HOST] Done.")
if received != total_bytes:
    print(f"[HOST] WARNING: Expected {total_bytes} bytes, got {received}. Image may be incomplete.")
else:
    print("[HOST] Image complete. You can now open it with hexdump/FDISK/mount/etc.")