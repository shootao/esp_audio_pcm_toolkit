# ESP Audio PCM Toolkit

- [中文版](./README_CN.md)

**esp_audio_pcm_toolkit** provides end-to-end tooling for **raw PCM capture** from Espressif boards and **remote audio parameter control** (playback volume, per-channel mic gain).

| Part | Location | Role |
|------|----------|------|
| Device library | `include/esp_audio_pcm.h` | Stream PCM over USB/UART; parse control commands |
| PC monitor | [`tools/pcm_monitor/`](./tools/pcm_monitor/) | Waveform, live listen, WAV save, send `vol` / `gain` / `gch` |

## Device API (ESP-IDF component)

```c
#include "esp_audio_pcm.h"

esp_audio_pcm_handle_t pcm = NULL;
esp_audio_pcm_config_t cfg = esp_audio_pcm_config_default();

ESP_ERROR_CHECK(esp_audio_pcm_new(&cfg, &pcm));
ESP_ERROR_CHECK(esp_audio_pcm_open(pcm));

const esp_audio_pcm_ctrl_cbs_t cbs = {
    .on_vol = my_vol_cb,
    .on_gain_all = my_gain_cb,
    .on_gain_channel = my_gch_cb,
    .on_stream = my_stream_cb,
};
ESP_ERROR_CHECK(esp_audio_pcm_ctrl_start(pcm, &cbs));

/* In record loop */
esp_audio_pcm_write(pcm, buffer, len, 20);
```

### Transports

| Type | Status | menuconfig |
|------|--------|------------|
| USB Serial/JTAG | Supported | `ESP Audio PCM Toolkit → USB Serial/JTAG` |
| UART | Supported | `ESP Audio PCM Toolkit → UART` |
| TCP | Reserved | — |
| UDP | Reserved | — |

### Control command format (text, one line)

```
vol 70
gain 30.0
gch 0 30.0
stream 1
```

Commands are read from the transport RX path and dispatched via callbacks. Responses are **not** echoed on the PCM port.

## PC monitor

See [`tools/pcm_monitor/README.md`](./tools/pcm_monitor/README.md).

Windows: double-click `tools/pcm_monitor/start_monitor.bat`.

## Example

[`examples/basic_example/`](./examples/basic_example/) — dummy PCM stream + remote control, no codec/BSP.

```bash
cd examples/basic_example && idf.py build flash monitor
```

## Dependencies

- ESP-IDF ≥ 5.0
- `esp_driver_usb_serial_jtag` and/or `esp_driver_uart`

## License

Espressif Modified MIT — see [LICENSE](./LICENSE).
