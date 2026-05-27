# Basic Example

Demonstrates **esp_audio_pcm_toolkit** with **esp_codec_dev** on Hi Nomi (ES8311 play + ES7210 record).

| Layer | API |
|-------|-----|
| Transport | `esp_audio_pcm_new/open/write`, `esp_audio_pcm_ctrl_start` |
| Audio | `esp_codec_dev_read`, `esp_codec_dev_set_out_vol`, `esp_codec_dev_set_in_channel_gain` |

If the board codec is unavailable, the example falls back to **dummy PCM** so it still builds and runs.

## PCM format

| Parameter | Default |
|-----------|---------|
| Sample rate | 16000 Hz |
| Channels | 4 (menuconfig: 3 or 4) |
| Bits | 16-bit signed LE |

Match **tools/pcm_monitor** (`Channels`, `Sample Rate`).

## Build & flash

Requires Hi Nomi board under `hi_nomi_record_play/components/hi_nomi_board`.

```bash
cd components/esp_audio_pcm_toolkit/examples/basic_example
idf.py set-target esp32s3
idf.py build flash monitor
```

- **UART0**: logs (`idf.py monitor`)
- **PCM transport** (menuconfig `ESP Audio PCM Toolkit → PCM stream transport`):
  - **USB** (default): PCM + remote commands on USB Serial/JTAG
  - **UART**: external USB-UART
  - **TCP / UDP**: WiFi required on target app; PC runs pcm_monitor as Server — see component README

## PC monitor

```text
../../tools/pcm_monitor/start_monitor.bat
```

- **Serial**: connect COM port
- **TCP/UDP**: select transport → **Start Server** → set device menuconfig PC IP + port 8766

Try: `vol 50`, `gain 25`, `gch 1 20`, `stream 0` — callbacks call `esp_codec_dev_set_out_vol` / `esp_codec_dev_set_in_channel_gain`.
