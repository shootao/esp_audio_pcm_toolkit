#!/usr/bin/env python3
"""Serve pcm_serial_monitor.html and bridge CDC serial via pyserial (Windows friendly)."""

from __future__ import annotations

import base64
import json
import sys
import threading
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed.", file=sys.stderr)
    print("Run: pip install pyserial", file=sys.stderr)
    raise SystemExit(1)


ROOT = Path(__file__).resolve().parent
DEFAULT_PORT = 8765
FALLBACK_PORTS = (8765, 8766, 8767, 9080, 18080, 18888)
ESPRESSIF_VID = 0x303A


class SerialBridge:
    def __init__(self) -> None:
        self._ser: serial.Serial | None = None
        self._lock = threading.Lock()

    def list_ports(self) -> list[dict[str, str]]:
        ports: list[dict[str, str]] = []
        for p in serial.tools.list_ports.comports():
            vid = f"0x{p.vid:04X}" if p.vid is not None else ""
            pid = f"0x{p.pid:04X}" if p.pid is not None else ""
            desc = p.description or "Serial Port"
            label = f"{p.device}  ({desc})"
            if p.vid == ESPRESSIF_VID:
                label = f"{p.device}  ({desc}) [ESP]"
            ports.append({
                "device": p.device,
                "label": label,
                "description": desc,
                "vid": vid,
                "pid": pid,
                "esp": p.vid == ESPRESSIF_VID,
            })

        ports.sort(key=lambda item: (not item["esp"], item["device"]))
        return ports

    def connect(self, port: str, baudrate: int) -> dict[str, object]:
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

    def write_cmd(self, line: str) -> dict[str, object]:
        text = line if line.endswith("\n") else line + "\n"
        with self._lock:
            if self._ser is None or not self._ser.is_open:
                return {"ok": False, "message": "Not connected"}
            try:
                self._ser.write(text.encode("utf-8"))
                self._ser.flush()
            except serial.SerialException as exc:
                return {"ok": False, "message": str(exc)}
        return {"ok": True}


BRIDGE = SerialBridge()


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def log_message(self, fmt: str, *args) -> None:
        if self.path.startswith("/api/"):
            return
        super().log_message(fmt, *args)

    def _send_json(self, code: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/ports":
            ports = BRIDGE.list_ports()
            self._send_json(200, {"ports": ports, "bridge": True, "count": len(ports)})
            return
        if path == "/api/read":
            data = BRIDGE.read()
            self._send_json(200, {"data": data})
            return
        if path in ("", "/"):
            self.path = "/pcm_serial_monitor.html"
        return super().do_GET()

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/connect":
            body = self._read_json()
            port = str(body.get("port", "")).strip()
            baud = int(body.get("baudrate", 115200))
            if not port:
                self._send_json(400, {"ok": False, "message": "Missing port"})
                return
            self._send_json(200, BRIDGE.connect(port, baud))
            return
        if path == "/api/disconnect":
            self._send_json(200, BRIDGE.disconnect())
            return
        if path == "/api/cmd":
            body = self._read_json()
            line = str(body.get("line", "")).strip()
            if not line:
                self._send_json(400, {"ok": False, "message": "Missing line"})
                return
            self._send_json(200, BRIDGE.write_cmd(line))
            return
        self.send_error(404)


def print_ports() -> None:
    ports = BRIDGE.list_ports()
    print("Detected serial ports:")
    if not ports:
        print("  (none) - check USB cable and Device Manager")
        return
    for p in ports:
        print(f"  {p['label']}")


def bind_server(preferred: int | None = None) -> tuple[ThreadingHTTPServer, int]:
    candidates: list[int] = []
    if preferred is not None:
        candidates.append(preferred)
    for port in FALLBACK_PORTS:
        if port not in candidates:
            candidates.append(port)

    last_error: OSError | None = None
    for port in candidates:
        try:
            server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
            server.allow_reuse_address = True
            return server, port
        except OSError as exc:
            last_error = exc
            print(f"Port {port} unavailable: {exc}")

    if last_error is not None:
        raise SystemExit(f"ERROR: no free TCP port ({last_error})")
    raise SystemExit("ERROR: no free TCP port")


def main() -> int:
    preferred = DEFAULT_PORT
    open_browser = True
    args = sys.argv[1:]
    if args and args[0] == "--no-browser":
        open_browser = False
        args = args[1:]
    if args:
        preferred = int(args[0])

    print_ports()
    print()

    BRIDGE.disconnect()
    server, port = bind_server(preferred)
    url = f"http://127.0.0.1:{port}/pcm_serial_monitor.html"
    (ROOT / "monitor.url").write_text(url + "\n", encoding="utf-8")
    print(f"PCM monitor bridge: {url}")
    print("Keep this window open. Close it to stop.")
    if open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        BRIDGE.disconnect()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
