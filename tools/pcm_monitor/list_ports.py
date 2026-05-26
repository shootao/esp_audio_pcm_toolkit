#!/usr/bin/env python3
"""List serial ports (diagnostic)."""

from __future__ import annotations

import sys

try:
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    raise SystemExit(1)


def main() -> int:
    ports = list(serial.tools.list_ports.comports())
    print("Serial ports on this PC:")
    print("-" * 60)
    if not ports:
        print("  (none found)")
        print()
        print("Check:")
        print("  1. USB cable connected (data port, not charge-only)")
        print("  2. Device Manager -> Ports (COM & LPT)")
        print("  3. ESP board powered and flashed")
        print("  4. Close idf.py monitor if it holds the COM port")
        return 1

    for p in ports:
        vid = f"0x{p.vid:04X}" if p.vid is not None else "n/a"
        pid = f"0x{p.pid:04X}" if p.pid is not None else "n/a"
        mark = "  <-- Espressif" if p.vid == 0x303A else ""
        print(f"  {p.device:8}  {p.description}")
        print(f"           hwid={p.hwid}  vid={vid} pid={pid}{mark}")
    print("-" * 60)
    print(f"Total: {len(ports)} port(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
