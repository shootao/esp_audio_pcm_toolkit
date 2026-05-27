#!/usr/bin/env python3
"""Serve pcm_serial_monitor.html; bridge Serial / TCP / UDP (device = client, PC = server)."""

from __future__ import annotations

import base64
import io
import json
import os
import platform
import re
import socket
import subprocess
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
MONITOR_HTML = ROOT / "pcm_serial_monitor.html"
DEFAULT_HTTP_PORT = 8765
DEFAULT_PCM_PORT = 8766
FALLBACK_PORTS = (8765, 8766, 8767, 9080, 18080, 18888)
ESPRESSIF_VID = 0x303A


def guess_local_ip() -> str:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("8.8.8.8", 80))
        return probe.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        probe.close()


def _is_usable_ipv4(ip: str) -> bool:
    if not ip or ip.count(".") != 3:
        return False
    if ip.startswith("127.") or ip.startswith("169.254."):
        return False
    parts = ip.split(".")
    try:
        nums = [int(p) for p in parts]
    except ValueError:
        return False
    return all(0 <= n <= 255 for n in nums)


def list_local_ipv4() -> list[dict[str, object]]:
    """Enumerate non-loopback IPv4 addresses on all interfaces."""
    seen: set[str] = set()
    addresses: list[dict[str, object]] = []
    primary = guess_local_ip()

    def push(ip: str, name: str, source: str) -> None:
        ip = ip.strip()
        if not _is_usable_ipv4(ip) or ip in seen:
            return
        seen.add(ip)
        addresses.append({
            "ip": ip,
            "name": name,
            "source": source,
            "primary": ip == primary,
        })

    try:
        for _, _, _, _, sa in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            if sa[0].startswith("127."):
                continue
            push(sa[0], "hostname", "resolved")
    except OSError:
        pass

    system = platform.system()
    if system == "Windows":
        _collect_windows_ipv4(push)
    else:
        _collect_unix_ipv4(push)

    if _is_usable_ipv4(primary) and primary not in seen:
        addresses.insert(0, {
            "ip": primary,
            "name": "default route",
            "source": "routed",
            "primary": True,
        })
    else:
        for item in addresses:
            item["primary"] = item["ip"] == primary

    addresses.sort(key=lambda item: (not bool(item["primary"]), str(item["name"]), str(item["ip"])))
    return addresses


def _collect_windows_ipv4(push) -> None:
    try:
        out = subprocess.check_output(
            ["ipconfig"],
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=5,
        )
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return

    adapter = "adapter"
    for raw in out.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.endswith(":") and "IPv4" not in line and "IP Address" not in line:
            adapter = line.rstrip(":")
            continue
        match = re.search(r"(?:IPv4 Address|IP Address|IPv4.*?地址)[^:：]*[:：]\s*([\d.]+)", line, re.I)
        if match:
            push(match.group(1), adapter, "ipconfig")


def _collect_unix_ipv4(push) -> None:
    try:
        out = subprocess.check_output(
            ["ip", "-4", "-o", "addr", "show"],
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=5,
        )
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 4 and parts[2] == "inet":
                iface = parts[1]
                if iface == "lo" or iface.startswith("lo."):
                    continue
                push(parts[3].split("/")[0], iface, "ip")
        return
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
        pass

    try:
        out = subprocess.check_output(
            ["ifconfig"],
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=5,
        )
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
        return

    iface = "iface"
    for raw in out.splitlines():
        if raw and not raw.startswith(("\t", " ")):
            iface = raw.split(":")[0]
            continue
        if iface == "lo" or iface.startswith("lo."):
            continue
        match = re.search(r"\binet (\d+\.\d+\.\d+\.\d+)", raw)
        if match:
            push(match.group(1), iface, "ifconfig")


class TransportBridge:
    """Unified bridge: serial COM, TCP server, or UDP server for PCM + text control."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._mode = "none"
        self._status_message = "Disconnected"

        self._ser: serial.Serial | None = None

        self._listen_sock: socket.socket | None = None
        self._client_sock: socket.socket | None = None
        self._client_addr: tuple[str, int] | None = None
        self._listen_port = DEFAULT_PCM_PORT
        self._rx_buf = bytearray()
        self._running = False
        self._worker: threading.Thread | None = None

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

    def list_ifaces(self) -> dict[str, object]:
        addresses = list_local_ipv4()
        primary = guess_local_ip()
        return {
            "addresses": addresses,
            "primary": primary,
            "count": len(addresses),
        }

    def capabilities(self) -> dict[str, object]:
        ifaces = self.list_ifaces()
        return {
            "bridge": True,
            "modes": ["serial", "tcp", "udp"],
            "local_ip": ifaces["primary"],
            "local_ips": ifaces["addresses"],
            "default_pcm_port": DEFAULT_PCM_PORT,
        }

    def status(self) -> dict[str, object]:
        with self._lock:
            client_connected = self._mode == "serial" and self._ser is not None and self._ser.is_open
            if self._mode == "tcp":
                client_connected = self._client_sock is not None
            elif self._mode == "udp":
                client_connected = self._client_addr is not None

            addr = ""
            if self._client_addr is not None:
                addr = f"{self._client_addr[0]}:{self._client_addr[1]}"

            return {
                "mode": self._mode,
                "listening": self._mode in ("tcp", "udp") and self._listen_sock is not None,
                "client_connected": client_connected,
                "client_addr": addr,
                "local_ip": guess_local_ip(),
                "local_ips": list_local_ipv4(),
                "listen_port": self._listen_port,
                "message": self._status_message,
            }

    def connect(self, body: dict) -> dict[str, object]:
        mode = str(body.get("mode", "serial")).lower()
        if mode == "serial":
            port = str(body.get("port", "")).strip()
            baud = int(body.get("baudrate", 115200))
            if not port:
                return {"ok": False, "message": "Missing port"}
            return self._connect_serial(port, baud)
        if mode == "tcp":
            pcm_port = int(body.get("port", DEFAULT_PCM_PORT))
            return self._start_tcp(pcm_port)
        if mode == "udp":
            pcm_port = int(body.get("port", DEFAULT_PCM_PORT))
            return self._start_udp(pcm_port)
        return {"ok": False, "message": f"Unknown mode: {mode}"}

    def disconnect(self) -> dict[str, bool]:
        self._stop_network()
        with self._lock:
            if self._ser is not None:
                try:
                    self._ser.close()
                except serial.SerialException:
                    pass
                self._ser = None
            self._mode = "none"
            self._status_message = "Disconnected"
        return {"ok": True}

    def read(self, max_bytes: int = 4096) -> str | None:
        with self._lock:
            if self._mode == "serial":
                if self._ser is None or not self._ser.is_open:
                    return None
                try:
                    data = self._ser.read(max_bytes)
                except serial.SerialException:
                    return None
                if not data:
                    return None
                return base64.b64encode(data).decode("ascii")

            if self._mode == "tcp":
                if self._client_sock is None:
                    return None
                try:
                    data = self._client_sock.recv(max_bytes)
                except (BlockingIOError, TimeoutError):
                    return None
                except OSError:
                    self._client_sock = None
                    self._client_addr = None
                    self._status_message = "TCP client disconnected"
                    return None
                if not data:
                    self._client_sock = None
                    self._client_addr = None
                    self._status_message = "TCP client disconnected"
                    return None
                return base64.b64encode(data).decode("ascii")

            if self._mode == "udp":
                if not self._rx_buf:
                    return None
                n = min(max_bytes, len(self._rx_buf))
                chunk = bytes(self._rx_buf[:n])
                del self._rx_buf[:n]
                return base64.b64encode(chunk).decode("ascii")

        return None

    def write_cmd(self, line: str) -> dict[str, object]:
        text = line if line.endswith("\n") else line + "\n"
        payload = text.encode("utf-8")

        with self._lock:
            if self._mode == "serial":
                if self._ser is None or not self._ser.is_open:
                    return {"ok": False, "message": "Not connected"}
                try:
                    self._ser.write(payload)
                    self._ser.flush()
                except serial.SerialException as exc:
                    return {"ok": False, "message": str(exc)}
                return {"ok": True}

            if self._mode == "tcp":
                if self._client_sock is None:
                    return {"ok": False, "message": "No TCP client yet"}
                try:
                    self._client_sock.sendall(payload)
                except OSError as exc:
                    return {"ok": False, "message": str(exc)}
                return {"ok": True}

            if self._mode == "udp":
                if self._client_addr is None:
                    return {"ok": False, "message": "No UDP peer yet (wait for PCM packets)"}
                try:
                    self._listen_sock.sendto(payload, self._client_addr)
                except OSError as exc:
                    return {"ok": False, "message": str(exc)}
                return {"ok": True}

        return {"ok": False, "message": "Not connected"}

    def _connect_serial(self, port: str, baudrate: int) -> dict[str, object]:
        self.disconnect()
        try:
            with self._lock:
                self._ser = serial.Serial(port, baudrate, timeout=0.02)
                self._mode = "serial"
                self._status_message = f"Connected {port} @ {baudrate}"
            return {"ok": True, "message": self._status_message}
        except serial.SerialException as exc:
            return {"ok": False, "message": str(exc)}

    def _stop_network(self) -> None:
        self._running = False
        if self._worker is not None:
            self._worker.join(timeout=1.0)
            self._worker = None

        with self._lock:
            if self._client_sock is not None:
                try:
                    self._client_sock.close()
                except OSError:
                    pass
                self._client_sock = None
            if self._listen_sock is not None:
                try:
                    self._listen_sock.close()
                except OSError:
                    pass
                self._listen_sock = None
            self._client_addr = None
            self._rx_buf.clear()

    def _start_tcp(self, port: int) -> dict[str, object]:
        self.disconnect()
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("0.0.0.0", port))
            sock.listen(1)
            sock.settimeout(0.5)
        except OSError as exc:
            sock.close()
            return {"ok": False, "message": f"TCP bind failed: {exc}"}

        with self._lock:
            self._listen_sock = sock
            self._listen_port = port
            self._mode = "tcp"
            ip = guess_local_ip()
            self._status_message = f"TCP listening on {ip}:{port}, waiting for device..."

        self._running = True
        self._worker = threading.Thread(target=self._tcp_accept_loop, daemon=True)
        self._worker.start()
        addrs = list_local_ipv4()
        return {
            "ok": True,
            "message": self._status_message,
            "local_ip": ip,
            "local_ips": addrs,
            "port": port,
        }

    def _start_udp(self, port: int) -> dict[str, object]:
        self.disconnect()
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("0.0.0.0", port))
            sock.settimeout(0.5)
        except OSError as exc:
            sock.close()
            return {"ok": False, "message": f"UDP bind failed: {exc}"}

        with self._lock:
            self._listen_sock = sock
            self._listen_port = port
            self._mode = "udp"
            ip = guess_local_ip()
            self._status_message = f"UDP listening on {ip}:{port}, waiting for device..."

        self._running = True
        self._worker = threading.Thread(target=self._udp_recv_loop, daemon=True)
        self._worker.start()
        addrs = list_local_ipv4()
        return {
            "ok": True,
            "message": self._status_message,
            "local_ip": ip,
            "local_ips": addrs,
            "port": port,
        }

    def _tcp_accept_loop(self) -> None:
        while self._running:
            listen_sock = None
            with self._lock:
                listen_sock = self._listen_sock
            if listen_sock is None:
                break
            try:
                client, addr = listen_sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break

            client.settimeout(0.02)
            with self._lock:
                if self._client_sock is not None:
                    try:
                        self._client_sock.close()
                    except OSError:
                        pass
                self._client_sock = client
                self._client_addr = addr
                self._status_message = f"TCP client connected: {addr[0]}:{addr[1]}"
            print(self._status_message)

    def _udp_recv_loop(self) -> None:
        while self._running:
            listen_sock = None
            with self._lock:
                listen_sock = self._listen_sock
            if listen_sock is None:
                break
            try:
                data, addr = listen_sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                break
            if not data:
                continue
            with self._lock:
                self._client_addr = addr
                self._rx_buf.extend(data)
                if len(self._rx_buf) > 4 * 1024 * 1024:
                    del self._rx_buf[: len(self._rx_buf) - 2 * 1024 * 1024]
                self._status_message = f"UDP peer: {addr[0]}:{addr[1]}"


BRIDGE = TransportBridge()


def monitor_html_version() -> str:
    try:
        return str(int(MONITOR_HTML.stat().st_mtime))
    except OSError:
        return "0"


def monitor_page_url(host: str, port: int) -> str:
    return f"http://{host}:{port}/pcm_serial_monitor.html?v={monitor_html_version()}"


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def log_message(self, fmt: str, *args) -> None:
        if self.path.startswith("/api/"):
            return
        super().log_message(fmt, *args)

    def end_headers(self) -> None:
        path = urlparse(self.path).path
        if path.endswith(".html") or path in ("", "/"):
            self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
            self.send_header("Pragma", "no-cache")
            self.send_header("Expires", "0")
        super().end_headers()

    def send_head(self):
        """Always send full HTML (never 304) so browser picks up local file edits."""
        parsed = urlparse(self.path)
        path = parsed.path
        if path in ("", "/"):
            path = "/pcm_serial_monitor.html"

        if path.endswith(".html"):
            old_path = self.path
            self.path = path
            file_path = self.translate_path(self.path)
            self.path = old_path
            ctype = self.guess_type(file_path)
            try:
                with open(file_path, "rb") as fh:
                    data = fh.read()
            except OSError:
                self.send_error(404, "File not found")
                return None
            self.send_response(200)
            self.send_header("Content-type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
            self.send_header("Pragma", "no-cache")
            self.send_header("Expires", "0")
            self.end_headers()
            return io.BytesIO(data)

        self.path = path
        return super().send_head()

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
            payload = BRIDGE.capabilities()
            payload.update({"ports": ports, "count": len(ports)})
            self._send_json(200, payload)
            return
        if path == "/api/ifaces":
            self._send_json(200, BRIDGE.list_ifaces())
            return
        if path == "/api/status":
            self._send_json(200, BRIDGE.status())
            return
        if path == "/api/read":
            data = BRIDGE.read()
            self._send_json(200, {"data": data})
            return
        if path == "/api/version":
            self._send_json(200, {
                "html": "pcm_serial_monitor.html",
                "html_version": monitor_html_version(),
                "html_path": str(MONITOR_HTML),
                "modes": ["serial", "tcp", "udp"],
            })
            return
        if path in ("", "/"):
            self.path = f"/pcm_serial_monitor.html?v={monitor_html_version()}"
        else:
            self.path = path
        return super().do_GET()

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/connect":
            body = self._read_json()
            self._send_json(200, BRIDGE.connect(body))
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
    preferred = DEFAULT_HTTP_PORT
    open_browser = True
    args = sys.argv[1:]
    if args and args[0] == "--no-browser":
        open_browser = False
        args = args[1:]
    if args:
        preferred = int(args[0])

    ip = guess_local_ip()
    print_ports()
    print()
    print("PC IPv4 addresses (for device menuconfig):")
    addrs = list_local_ipv4()
    if not addrs:
        print(f"  (none detected, routed guess: {ip})")
    else:
        for item in addrs:
            mark = " *recommended*" if item.get("primary") else ""
            print(f"  {item['ip']}  ({item['name']}){mark}")
    print()
    print(f"Default PCM TCP/UDP port: {DEFAULT_PCM_PORT}")
    print(f"Monitor HTML: {MONITOR_HTML}")
    print(f"Monitor HTML version: {monitor_html_version()}")
    print()

    BRIDGE.disconnect()
    server, port = bind_server(preferred)
    url = monitor_page_url("127.0.0.1", port)
    (ROOT / "monitor.url").write_text(url + "\n", encoding="utf-8")
    print(f"PCM monitor bridge: {url}")
    print("Keep this window open. Close it to stop.")
    print("If the page looks old: close browser tab, close this window, re-run start_monitor.bat")
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
