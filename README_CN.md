# ESP Audio PCM Toolkit

- [English](./README.md)

**esp_audio_pcm_toolkit** 用于 Espressif 开发板的 **原始 PCM 录音流传输** 与 **远程音频参数调节**（播放音量、各通道 Mic 增益）。

| 部分 | 路径 | 作用 |
|------|------|------|
| 设备端库 | `include/esp_audio_pcm.h` | USB / UART / TCP / UDP 传 PCM；解析控制命令 |
| PC 监控 | [`tools/pcm_monitor/`](./tools/pcm_monitor/) | 波形、试听、存 WAV、下发 `vol` / `gain` / `gch` |

当前版本：**v0.2.4**（详见 [CHANGELOG](./CHANGELOG.md)）。

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

`esp_audio_pcm_config_default()` 会跟随 menuconfig 中的传输方式自动填充 USB / UART / TCP / UDP 参数。

### 传输方式

| 类型 | 状态 | menuconfig |
|------|------|------------|
| USB Serial/JTAG | 已支持 | `ESP Audio PCM Toolkit → USB Serial/JTAG` |
| UART | 已支持 | `ESP Audio PCM Toolkit → UART` |
| TCP | 已支持 | 设备 Client → PC Server（见 PC 监控） |
| UDP | 已支持 | 设备 Client → PC Server（见 PC 监控） |

**USB / UART 注意**

- 发 PCM 的那一路 **不能混 Log/Console 文本**，否则 PC 波形会乱码。开始推流前建议 `esp_log_level_set("*", ESP_LOG_NONE)`，或把 Console 改到另一路 UART。
- **USB CDC**：波特率对吞吐几乎无影响；PC monitor 填 **115200** 即可。
- **硬件 UART**：默认波特率 **921600**（`ESP_AUDIO_PCM_UART_BAUD`）。16 kHz / 4 ch / 16 bit 约 128 KB/s，须选足够高的 baud，否则 TX 缓冲满会丢样；PC monitor 的 **Baud Rate 须与固件一致**。

**TCP/UDP 注意**

- 应用需先连 **WiFi**，再调用 `esp_audio_pcm_open()`。
- menuconfig 中配置 **PC monitor server IP** 与端口（默认 **8766**）。
- PC 停止 Server 后再启动：**TCP** 设备会在下次 `esp_audio_pcm_write()` 时自动重连；**UDP** 一般无需重启设备。
- **TCP 发送缓冲**：有 PSRAM 时默认约 **1 MiB** 环形缓冲 + 后台发送任务，缓解 WiFi 拥塞；menuconfig `TCP TX ring buffer size` 可调。
- **TCP 满缓冲策略**（v0.2.4）：默认 `Block when TCP TX ring is full`，录音侧会等待而不是丢样；可在 menuconfig 关闭以改为丢弃并打 `esp_audio_pcm_tcp` 告警。

网络相关 menuconfig（选 TCP/UDP 时出现）：

| 配置项 | 说明 |
|--------|------|
| `PC monitor server IP` | 运行 pcm_monitor 的 PC 局域网 IPv4 |
| `TCP server port` / `UDP PCM port` | 与网页监听端口一致（默认 8766） |
| `TCP TX ring buffer size` | TCP 发送环形缓冲（默认 1048576 B，优先 PSRAM） |
| `Block when TCP TX ring is full` | 环满时阻塞 `esp_audio_pcm_write`（默认开） |
| `UDP local bind port` | `0` 表示自动分配（同 socket 收控制命令） |

UART 相关 menuconfig（选 UART 时出现）：

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `UART port number` | 1 | UART 端口号 |
| `UART TX GPIO` / `RX GPIO` | 17 / 18 | 按板级原理图修改 |
| `UART baud rate` | **921600** | PC monitor 须填相同值 |
| `UART TX buffer size` | 4096 | 可按帧长加大 |

### 控制命令格式（文本，一行一条）

```
vol 70
gain 30.0
gch 0 30.0
stream 1
```

从传输 RX 读入，经回调交给应用；**不在 PCM 口回显**，避免污染波形。

## PC 监控

完整说明见 [`tools/pcm_monitor/README.md`](./tools/pcm_monitor/README.md)。

![PCM Monitor Dashboard](./tools/pcm_monitor/__static/esp_pcm_monitor.png)

*PC 端网页工具：Serial / TCP / UDP 收 PCM，多通道波形、实时试听、WAV 导出与远程音量/增益控制。*

**启动**

| 平台 | 命令 |
|------|------|
| Windows | 双击 `tools/pcm_monitor/start_monitor.bat` |
| Linux / macOS | `tools/pcm_monitor/start_monitor.sh` |

浏览器会打开 **PCM Monitor Dashboard**（Material 风格布局：顶栏状态 pills + 左侧 Connection/PCM/Audio Control + 右侧波形与统计）。

| 网页传输方式 | PC 角色 | 操作 |
|--------------|---------|------|
| Serial (USB/UART) | 打开 COM 口 | 选串口 → **Connect** |
| TCP / UDP | 监听 `0.0.0.0:8766` | **Start TCP/UDP Server**，等待设备连入 |

网页 **Audio Control** 区：调整音量/增益后须点 **Apply Controls** 才会下发（滑条本身不会自动发送）。

## 示例与参考工程

| 工程 | 板卡 | 说明 |
|------|------|------|
| [`examples/basic_example/`](./examples/basic_example/) | 任意 ESP32-S3 | 最小 dummy PCM + 远程控制（无 BSP） |
| [`test/hi_nomi_record_play/main/`](../../main/) | Hi Nomi | 完整 demo：SPIFFS 播放、Console、WiFi + TCP/UDP |
| [`test/esp32_s3_korvo_2_board/`](../../../esp32_s3_korvo_2_board/) | ESP32-S3-Korvo-2 | Korvo BSP + ES7210 四麦；默认 TCP；**仅引用本组件目录** |

在其他工程复用本组件时，`EXTRA_COMPONENT_DIRS` 请指向 **`esp_audio_pcm_toolkit` 目录本身**，不要包含整个 `components/` 树（否则会误链入 `hi_nomi_board` 等同名 BSP）：

```cmake
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../hi_nomi_record_play/components/esp_audio_pcm_toolkit")
```

编译示例：

```bash
cd examples/basic_example && idf.py build flash monitor
```

## 依赖

- ESP-IDF ≥ 5.0
- `esp_driver_usb_serial_jtag`（USB）
- `esp_driver_uart`（UART）
- `lwip`（TCP/UDP）
- 可选 PSRAM（TCP 大环缓）

## 许可证

Espressif Modified MIT — 见 [LICENSE](./LICENSE)。
