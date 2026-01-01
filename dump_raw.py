#!/usr/bin/env python3
# dump_raw.py  —  Byte-exact SD card dump via Teensy serial
#
# Usage:  python3 dump_raw.py /dev/tty.usbmodemXXXX dump_raw.img
#
# Requirements: pyserial  (pip install pyserial)

import os, sys, time, serial, struct

CHUNK = 1 << 16      # 64 KiB reads from serial
BAUD  = 115200       # must match your sketch's Serial.begin()

def die(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)

def read_exact(ser, n):
    """Read exactly n bytes from serial or die."""
    out = bytearray()
    while len(out) < n:
        chunk = ser.read(n - len(out))
        if not chunk:
            die(f"[ERR] Serial timeout after {len(out)}/{n} bytes")
        out.extend(chunk)
    return bytes(out)

def human(n):
    for unit in ("B","KiB","MiB","GiB"):
        if n < 1024 or unit == "GiB":
            return f"{n:.2f} {unit}"
        n /= 1024.0

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <serial_port> <output_img>")
        sys.exit(1)

    port = sys.argv[1]
    out_path = sys.argv[2]

    print(f"[INFO] Opening {port} @ {BAUD}…")
    ser = serial.Serial(port, BAUD, timeout=2)
    time.sleep(0.3)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    # Optional: init card once (non-binary chatter ok before we start)
    ser.write(b"i")
    ser.flush()
    time.sleep(0.2)
    ser.reset_input_buffer()

    # 1) Get 4-byte little-endian block count (no text around it)
    ser.write(b"n")               # Teensy must respond with 4 raw bytes only
    ser.flush()
    bc = read_exact(ser, 4)
    blocks = struct.unpack("<I", bc)[0]
    if blocks == 0:
        die("[ERR] Teensy reported zero blocks")
    total = blocks * 512
    print(f"[INFO] Blocks: {blocks}  ({human(total)})")

    # 2) Start the raw-only dump and read exactly total bytes
    ser.reset_input_buffer()
    ser.write(b"R")               # Teensy must stream raw bytes only
    ser.flush()

    print(f"[INFO] Dumping to {out_path} …")
    start = time.time()
    received = 0
    last_print = 0

    with open(out_path, "wb") as f:
        while received < total:
            need = min(CHUNK, total - received)
            buf = ser.read(need)
            if not buf:
                # one retry window
                buf = ser.read(need)
                if not buf:
                    print()
                    die(f"[ERR] Serial stalled at {received}/{total} bytes")
            f.write(buf)
            received += len(buf)

            now = time.time()
            if now - last_print >= 0.25 or received == total:
                pct = (received / total) * 100.0
                rate = received / max(1e-6, (now - start))
                print(f"\r[INFO] {received}/{total}  {pct:5.1f}%  {human(rate)}/s", end="")
                last_print = now

    dur = time.time() - start
    print(f"\n[OK] Wrote {human(total)} in {dur:.1f}s  ({human(total/dur)}/s)")

    # 3) Quick sanity check: read first 512 bytes for 0x55AA signature
    with open(out_path, "rb") as f:
        boot = f.read(512)
    sig = boot[510:512] if len(boot) >= 512 else b""
    print(f"[CHECK] Sector0 signature: {sig.hex().upper()}  "
          f"{'(looks like MBR/VBR)' if sig == b'\\x55\\xAA' else '(no 55AA, may still be valid)'}")

if __name__ == "__main__":
    main()