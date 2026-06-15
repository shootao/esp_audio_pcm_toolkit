#!/usr/bin/env bash
# ESP PCM Monitor — Linux / macOS launcher (mirrors start_monitor.bat)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

SCRIPT="$ROOT/pcm_serial_bridge.py"
LIST="$ROOT/list_ports.py"
URLFILE="$ROOT/monitor.url"

echo "ESP PCM Serial Monitor"
echo "Folder: $ROOT"
echo

find_python() {
  if command -v python3 >/dev/null 2>&1; then
    echo python3
    return 0
  fi
  if command -v python >/dev/null 2>&1; then
    echo python
    return 0
  fi
  return 1
}

PYRUN="$(find_python)" || {
  echo "ERROR: Python 3 not found. Install python3 and retry."
  exit 1
}

echo "Using Python: $PYRUN"

if ! "$PYRUN" -c "import serial" 2>/dev/null; then
  echo "Installing pyserial..."
  "$PYRUN" -m pip install pyserial
fi

echo
echo "=== Serial port scan ==="
"$PYRUN" "$LIST"
echo

rm -f "$URLFILE"

echo "Starting bridge..."
"$PYRUN" "$SCRIPT" --no-browser &
BRPID=$!

cleanup() {
  kill "$BRPID" 2>/dev/null || true
  wait "$BRPID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "Waiting for bridge URL..."
BRURL=""
for _ in $(seq 1 20); do
  if [[ -f "$URLFILE" ]]; then
    BRURL="$(tr -d '\r\n' < "$URLFILE")"
    break
  fi
  sleep 1
done

if [[ -z "$BRURL" ]]; then
  echo "WARNING: monitor.url not found. Check bridge output above for errors."
  echo "Try: http://127.0.0.1:8765/pcm_serial_monitor.html?v=1"
  echo "Stop any old bridge (pkill -f pcm_serial_bridge.py) and re-run this script."
  exit 1
fi

echo
echo "Bridge URL: $BRURL"
echo "Opening browser..."

if command -v xdg-open >/dev/null 2>&1; then
  xdg-open "$BRURL" >/dev/null 2>&1 &
elif command -v open >/dev/null 2>&1; then
  open "$BRURL"
else
  echo "No xdg-open/open found. Open the URL above manually in Chrome or Edge."
fi

echo
echo "Bridge running (pid $BRPID). Press Ctrl+C to stop."
echo "If the port list is empty, click Refresh or type /dev/ttyACM0 in Port field."
wait "$BRPID"
