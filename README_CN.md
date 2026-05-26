# ESP Audio PCM Toolkit

- [English](./README.md)

**esp_audio_pcm_toolkit** 用于 Espressif 开发板的 **原始 PCM 录音流传输** 与 **远程音频参数调节**（播放音量、各通道 Mic 增益）。

| 部分 | 路径 | 作用 |
|------|------|------|
| 设备端库 | `include/esp_audio_pcm.h` | USB/UART 传 PCM；解析控制命令 |
| PC 监控 | [`tools/pcm_monitor/`](./tools/pcm_monitor/) | 波形、试听、存 WAV、下发 `vol` / `gain` / `gch` |

## 设备端 API（ESP-IDF 组件）

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

/* 录音循环中 */
esp_audio_pcm_write(pcm, buffer, len, 20);
```

### 传输方式

| 类型 | 状态 | menuconfig |
|------|------|------------|
| USB Serial/JTAG | 已支持 | `ESP Audio PCM Toolkit → USB Serial/JTAG` |
| UART | 已支持 | `ESP Audio PCM Toolkit → UART` |
| TCP | 预留 | — |
| UDP | 预留 | — |

### 控制命令格式（文本，一行一条）

```
vol 70
gain 30.0
gch 0 30.0
stream 1
```

从传输 RX 读入，经回调交给应用；**不在 PCM 口回显**，避免污染波形。

## PC 监控

见 [`tools/pcm_monitor/README.md`](./tools/pcm_monitor/README.md)。

Windows：双击 `tools/pcm_monitor/start_monitor.bat`。

## 示例

[`examples/basic_example/`](./examples/basic_example/) — 发送 dummy PCM 并响应远程控制，无需 codec/板级 BSP。

```bash
cd examples/basic_example && idf.py build flash monitor
```

## 依赖

- ESP-IDF ≥ 5.0
- `esp_driver_usb_serial_jtag` / `esp_driver_uart`

## 许可证

Espressif Modified MIT — 见 [LICENSE](./LICENSE)。
