# ESP Audio PCM Toolkit

- [English](./README.md)

**esp_audio_pcm_toolkit** 用于 Espressif 开发板的 **原始 PCM 录音流传输** 与 **远程音频参数调节**（播放音量、各通道 Mic 增益）。

| 部分 | 路径 | 作用 |
|------|------|------|
| 设备端库 | `include/esp_audio_pcm.h` | USB / UART / TCP / UDP 传 PCM；解析控制命令 |
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
| TCP | 已支持 | 设备 Client → PC Server（见 PC 监控） |
| UDP | 已支持 | 设备 Client → PC Server（见 PC 监控） |

**TCP/UDP 说明**

- 应用需先连 **WiFi**，再调用 `esp_audio_pcm_open()`。
- menuconfig 中配置 **PC monitor server IP** 与端口（默认 **8766**）。
- PC 停止 Server 后再启动：**TCP** 设备会在下次发 PCM 时自动重连；**UDP** 一般无需重启设备。

网络相关 menuconfig（选 TCP/UDP 时出现）：

| 配置项 | 说明 |
|--------|------|
| `PC monitor server IP` | 运行 pcm_monitor 的 PC 局域网 IPv4 |
| `TCP server port` / `UDP PCM port` | 与网页监听端口一致（默认 8766） |
| `UDP local bind port` | `0` 表示自动分配（同 socket 收控制命令） |

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

| 网页传输方式 | PC 角色 | 操作 |
|--------------|---------|------|
| Serial (USB/UART) | 打开 COM 口 | **连接 COM 口** |
| TCP / UDP | 监听 `0.0.0.0:端口` | **启动 TCP/UDP Server**，等待设备连入 |

网页 **设备控制** 区：调整音量/增益后须点 **Apply to device** 才会下发（滑条本身不会自动发送）。

## 示例

[`examples/basic_example/`](./examples/basic_example/) — Hi Nomi BSP + `esp_codec_dev` 录音，PCM 推流与远程控制。

```bash
cd examples/basic_example && idf.py build flash monitor
```

完整工程参考：[`test/hi_nomi_record_play/main/`](../../main/)（menuconfig 可选 TCP/UDP + WiFi）。

## 依赖

- ESP-IDF ≥ 5.0
- `esp_driver_usb_serial_jtag` / `esp_driver_uart` / `lwip`（TCP/UDP）

## 许可证

Espressif Modified MIT — 见 [LICENSE](./LICENSE)。
