#!/usr/bin/env python3
"""Launch PCM monitor UI with native CDC serial (no HTTP server)."""

from __future__ import annotations

import base64
import sys
import threading
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Missing pyserial. Install: pip install pyserial", file=sys.stderr)
    raise SystemExit(1)

try:
    import webview
except ImportError:
    print("Missing pywebview. Install: pip install pywebview", file=sys.stderr)
    raise SystemExit(1)


HTML_PATH = Path(__file__).with_name("pcm_serial_monitor.html")


class SerialApi:
    def __init__(self) -> None:
        self._ser: serial.Serial | None = None
        self._lock = threading.Lock()

    def list_ports(self) -> list[dict[str, str]]:
        ports = []
        for p in serial.tools.list_ports.comports():
            label = p.device
            if p.description and p.description != "n/a":
                label = f"{p.device}  ({p.description})"
            ports.append({"device": p.device, "label": label})
        return ports

    def connect(self, port: str, baudrate: int = 115200) -> dict[str, str | bool]:
        self.disconnect()
        try:
            with self._lock:
                self._ser = serial.Serial(port, baudrate, timeout=0.02)
            return {"ok": True, "message": f"Connected {port} @ {baudrate}"}
        except serial.SerialException as exc:
            return {"ok": False, "message": str(exc)}

    def disconnect(self) -> dict[str, bool]:
        with self._lock:
            if self._ser is not None:
                try:
                    self._ser.close()
                except serial.SerialException:
                    pass
                self._ser = None
        return {"ok": True}

    def read(self, max_bytes: int = 4096) -> str | None:
        with self._lock:
            if self._ser is None or not self._ser.is_open:
                return None
            try:
                data = self._ser.read(max_bytes)
            except serial.SerialException:
                return None
        if not data:
            return None
        return base64.b64encode(data).decode("ascii")

    def send_cmd(self, line: str) -> dict[str, str | bool]:
        text = line if line.endswith("\n") else line + "\n"
        with self._lock:
            if self._ser is None or not self._ser.is_open:
                return {"ok": False, "message": "Not connected"}
            try:
                self._ser.write(text.encode("utf-8"))
                self._ser.flush()
            except serial.SerialException as exc:
                return {"ok": False, "message": str(exc)}
        return {"ok": True, "message": "sent"}

    def is_connected(self) -> bool:
        with self._lock:
            return self._ser is not None and self._ser.is_open


def main() -> int:
    if not HTML_PATH.is_file():
        print(f"Missing {HTML_PATH}", file=sys.stderr)
        return 1

    api = SerialApi()
    webview.create_window(
        "ESP PCM Serial Monitor",
        url=str(HTML_PATH.resolve().as_uri()),
        js_api=api,
        width=1180,
        height=760,
        min_size=(900, 620),
    )
    webview.start(gui="qt" if sys.platform.startswith("linux") else None)
    api.disconnect()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
