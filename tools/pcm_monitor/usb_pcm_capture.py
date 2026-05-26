#!/usr/bin/env python3
"""Capture interleaved PCM from ESP USB Serial/JTAG and save for Audacity."""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("Install pyserial: pip install pyserial", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--port", required=True, help="USB serial port, e.g. /dev/ttyACM0")
    parser.add_argument("-o", "--output", default="capture.pcm", help="Output PCM file")
    parser.add_argument("-c", "--channels", type=int, default=4, choices=[3, 4])
    parser.add_argument("--rate", type=int, default=16000)
    args = parser.parse_args()

    ser = serial.Serial(args.port, baudrate=115200, timeout=1)
    print(f"Reading from {args.port}")
    print(f"Audacity import: Signed 16-bit PCM, Little-endian, {args.channels} channels, {args.rate} Hz")
    print(f"Saving to {args.output}. Ctrl+C to stop.")

    total = 0
    last_print = time.monotonic()
    frame_bytes = args.channels * 2

    try:
        with open(args.output, "wb") as out:
            while True:
                data = ser.read(4096)
                if not data:
                    continue
                out.write(data)
                total += len(data)
                now = time.monotonic()
                if now - last_print >= 1.0:
                    seconds = total / (args.rate * frame_bytes)
                    print(f"\r{total} bytes ({seconds:.1f}s)", end="")
                    sys.stdout.flush()
                    last_print = now
    except KeyboardInterrupt:
        seconds = total / (args.rate * frame_bytes)
        print(f"\nDone: {total} bytes ({seconds:.1f}s)")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
