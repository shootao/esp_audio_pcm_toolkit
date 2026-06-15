# Basic Example

Minimal **esp_audio_pcm_toolkit** demo — **no board BSP required**. Synthetic audio only:

| Path | Waveform | Default |
|------|----------|---------|
| **Record → PC** | Sine per channel | 4 ch @ 220 / 440 / 660 / 880 Hz |
| **Play (sim)** | Square | 440 Hz, amplitude from `vol` |

No codec/I2S init. `vol` / `gain` / `gch` callbacks adjust tone levels (logged + applied to sine amplitude).

| Layer | API |
|-------|-----|
| Transport | `esp_audio_pcm_new/open/write`, `esp_audio_pcm_ctrl_start` |
| Audio source | Built-in sine (record) + square-wave play sim in `basic_example.c` |
| Network | `app_net_init()` when transport is TCP/UDP |

## PCM format

| Parameter | Default |
|-----------|---------|
| Sample rate | 16000 Hz |
| Channels | 4 (menuconfig: 3 or 4) |
| Bits | 16-bit signed LE |

Match **tools/pcm_monitor** (`Channels`, `Sample Rate`).

## Component paths

| File | Purpose |
|------|---------|
| [`main/idf_component.yml`](./main/idf_component.yml) | `esp_audio_pcm_toolkit` via `override_path: "../../.."` |
| [`CMakeLists.txt`](./CMakeLists.txt) | `EXTRA_COMPONENT_DIRS` → toolkit root only |

No `hi_nomi_board`, no `esp_codec_dev` — runs on any ESP32-S3 (or retarget) with the selected PCM transport.

## Build & flash

```bash
cd components/esp_audio_pcm_toolkit/examples/basic_example
idf.py set-target esp32s3
idf.py build flash monitor
```

### menuconfig

| Menu | Notes |
|------|-------|
| **ESP Audio PCM Toolkit → PCM stream transport** | USB (default) / UART / TCP / UDP |
| **Basic Example → Record channel count** | Must match PC monitor **Channels** |
| **Basic Example → WiFi SSID / password** | When transport is TCP or UDP |
| **ESP Audio PCM Toolkit → UART baud rate** | Default **921600** for hardware UART |

### Transport vs PC monitor

| Transport | Logs | PC monitor |
|-----------|------|------------|
| **USB** | UART0 | Serial, baud **115200** |
| **UART** | `ESP_LOG_NONE` on PCM UART | Serial, baud **921600** |
| **TCP / UDP** | UART0 | **Start TCP/UDP Server**, device **Server IP** + **8766** |

## PC monitor

```bash
../../tools/pcm_monitor/start_monitor.sh
# Windows: ../../tools/pcm_monitor/start_monitor.bat
```

Try `vol 50`, `gain 25`, `gch 1 20`, `stream 0` — callbacks update local state and log (dummy hardware).

## Add real hardware

Swap `fill_sine_pcm()` / `play_task` in [`main/basic_example.c`](./main/basic_example.c) for your BSP + `esp_codec_dev`. See full integrations:

- [`test/hi_nomi_record_play/main/`](../../../../main/)
- [`test/esp32_s3_korvo_2_board/`](../../../../../esp32_s3_korvo_2_board/)
