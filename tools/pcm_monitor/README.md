# PCM Monitor（PC 端）

本目录为 **esp_audio_pcm_toolkit** 组件的 PC 端工具，与设备端 `esp_audio_pcm_*` API 配套使用。

支持三种传输方式：

| 方式 | 数据方向 | 网页操作 |
|------|----------|----------|
| **Serial (USB/UART)** | ESP → PC（CDC/UART） | 选 COM 口 → **连接 COM 口** |
| **TCP** | ESP 连 PC Server，双向 | 选 TCP → **启动 TCP Server** |
| **UDP** | ESP 发包到 PC，控制命令 UDP 回包 | 选 UDP → **启动 UDP Server** |

通过上述链路接收 ESP 固件输出的 PCM，在 PC 上显示波形、试听、保存 WAV，并下发 `vol` / `gain` / `gch` 控制命令。

---

## 怎么用（Windows）

**直接双击 `start_monitor.bat`**，浏览器会自动打开（URL 带 `?v=` 防缓存）。

> 不要直接双击 `.html` 文件。更新 HTML 后请 **关闭旧的「PCM Monitor」黑窗口**，再重新运行 bat。

### Serial（USB / UART）

1. 传输方式选 **Serial (USB/UART)**
2. 选 COM 口（或填 `COM23`）→ **连接 COM 口**
3. 使用前：烧好固件、关闭 `idf.py monitor`

### TCP / UDP（已验证）

架构：**PC = Server，ESP = Client**（同一 WiFi / 局域网）。

1. 传输方式选 **TCP** 或 **UDP**
2. 确认 **PCM 监听端口**（默认 **8766**，须与设备 menuconfig 一致）
3. 在 **PC 本机 IP** 列表中选与 ESP 同网段的地址（或手动覆盖）
4. 点 **启动 TCP Server** / **启动 UDP Server**
5. 设备 menuconfig：
   - `ESP Audio PCM Toolkit → PCM stream transport` → TCP 或 UDP
   - `PC monitor server IP` → 网页上显示的 PC IP
   - 端口与网页一致
   - （`hi_nomi_record_play`）`Hi Nomi Record Play → WiFi SSID / password`
6. 设备上电连 WiFi 后自动推 PCM；状态栏显示「设备已连接」

**停止 / 再开 Server**

| 模式 | 设备是否要重启 |
|------|----------------|
| TCP | 一般 **不用**；下次发 PCM 会自动重连 |
| UDP | **不用**；继续发包即可 |

桥接黑窗口会打印本机 IP 列表；也可访问 `http://127.0.0.1:8765/api/version` 确认桥接版本（含 `modes: serial/tcp/udp`）。

### ⚠️ Serial 模式：固件必须关闭所有 Log

USB/UART 口传的是**纯 PCM 二进制**，任何日志文字混进去都会导致波形乱码。

TCP/UDP 网络口不经过 USB CDC，Console/Log 可仍走 UART；但 **PCM 链路上仍不要打印 Log**。

开始发 PCM **之前**（Serial 模式尤其重要）：

```c
esp_log_level_set("*", ESP_LOG_NONE);
```

---

## 工具脚本

### 方式一：双击 bat（Windows，推荐）

| 双击这个 | 作用 |
|---------|------|
| **`start_monitor.bat`** | 启动监控，自动打开浏览器 |
| **`list_ports.bat`** | 查看 COM 口（排查用） |

### 方式二：命令行

```bash
# 启动监控（Linux / macOS / Windows 均可）
python3 pcm_serial_bridge.py

# 查看 COM 口
python3 list_ports.py

# 录 PCM 到文件（不打开网页）
python3 usb_pcm_capture.py -p COM23 -o capture.pcm -c 4 --rate 16000
```

Windows 下若 `python3` 不可用，可改为 `python` 或 `py -3`。

---

## PCM 参数（须与固件一致）

| 参数 | 常见示例 | 说明 |
|------|----------|------|
| Sample Rate | **16000** | 采样率（Hz），不是串口波特率 |
| Channels | **1–4**（按固件） | 多通道交错 PCM |
| Bits | **16** | 有符号 16 位 |
| Endian | **Little-endian** | 小端 |
| 串口波特率 | **115200** | 仅用于 CDC 握手，与采样率无关 |

固件常见：`16000 Hz / 16 bit / N ch`，交错格式（Ch0_s0, Ch1_s0, …, Ch0_s1, …）。

---

## 网页功能

- **传输方式**：Serial / TCP / UDP（TCP/UDP 时隐藏 COM 口区域）
- **波形**：多通道分轨显示，支持滚轮缩放、Shift+滚轮平移
- **Wave Window**：控制可见时间窗口（1–8 秒）
- **暂停**：冻结波形与录制缓冲
- **开启实时播放**：浏览器内试听（16 kHz → 重采样到 ~48 kHz）
- **Save WAV**：保存当前缓冲为 WAV
- **设备控制**（连接 Serial / TCP / UDP 后）：
  - **播放音量** 0–100（%）、**Ch0 – Ch3** _mic 增益滑条（与 `gch 0`…`gch 3` 对应）
  - **重要：拖动滑条或改数字不会立刻生效**，须再点红色 **Apply to device**，才会向设备发送 `vol` / `gch` 命令
  - 适用于 Serial、TCP、UDP（命令走同一传输 RX，不在 PCM 流里回显）

下发命令格式（与 UART Console 一致，若固件实现了对应命令）：

| 命令 | 说明 |
|------|------|
| `vol 70` | 播放音量 0–100 |
| `gch 0 30.0` | Ch0 增益 dB |
| `gch 1 30.0` | Ch1 |
| `gch 2 30.0` | Ch2 |
| `gch 3 30.0` | Ch3（4 通道时） |
| `gain 30.0` | 所有通道同一增益 |

---

## 常见问题

### 改了音量/增益，设备没反应

- 网页滑条**不会自动下发**；改完后必须点 **Apply to device**
- 确认已连接（Serial 已连 COM，或 TCP/UDP 已显示「设备已连接」）
- 串口模式下确认 `idf.py monitor` 未占用 COM

### 页面显示「页面初始化失败」

- 确认通过 `start_monitor.bat` 打开的 URL 访问，而非旧书签或 `file://`
- 关闭浏览器标签页，重新运行 `start_monitor.bat`
- 按 F12 查看 Console 是否有报错

### 页面仍是旧版（没有 TCP/UDP 下拉框）

1. 关闭浏览器标签页
2. 关闭标题为 **PCM Monitor** 的 cmd 窗口
3. 重新双击 **`start_monitor.bat`**
4. 使用黑窗口里带 **`?v=`** 的 URL；标题应含 **Audio PCM Monitor**

### COM 口列表为空

1. 运行 `list_ports.bat` 或 `python3 list_ports.py` 查看系统是否识别端口
2. 检查 USB 线、设备管理器 → **端口 (COM 和 LPT)**
3. 关闭 `idf.py monitor` 及其他占用 COM 的程序
4. 手动输入 `COMxx` 后点 **刷新** → **连接**

### 波形异常（块状、乱码、有文字）

- **最常见原因：固件 Log 没关干净**，检查是否已 `esp_log_level_set("*", ESP_LOG_NONE)`
- 确认 PCM 和 Log/Console 没有共用同一个 USB CDC 口
- 确认 Sample Rate 填 **16000**，不要填 115200

### TCP/UDP：一直「等待设备连接」

- PC 与 ESP 是否同一网段（PC IP 是否选对）
- 设备 menuconfig **Server IP / 端口** 是否与网页一致
- 设备 WiFi 是否已连接（串口 log 应有 `got ip`）
- PC 防火墙是否放行入站 **8766**（或你设的端口）
- 先点网页 **启动 Server**，再让设备 `esp_audio_pcm_open()`

### 端口被占用（WinError 10013）

桥接会自动尝试 8765、8766、8767 等端口。以 `monitor.url` 或桥接窗口输出的 URL 为准。

### 无声音（实时播放）

- 点击 **开启实时播放** 后，浏览器可能需用户交互才能启动 AudioContext
- 确认已 **连接** 且波形在动

---

## 依赖

- Python 3.8+
- `pyserial`（`start_monitor.bat` 会自动安装）
- 可选：`pywebview`（仅 `pcm_serial_monitor.py` 需要）

---

## 固件侧参考

推荐使用本组件 **`esp_audio_pcm_*` API**（`esp_audio_pcm_config_default()` 跟随 menuconfig 选传输方式）。

| 方式 | 底层 | 典型场景 |
|------|------|----------|
| **USB Serial/JTAG** | `esp_driver_usb_serial_jtag` | USB 直连 ESP（S3/C3/C5 等） |
| **UART** | `esp_driver_uart` | 外接 USB-UART 或第二路 UART |
| **TCP** | `lwip` socket，connect PC Server | WiFi 板卡，PC 跑 pcm_monitor |
| **UDP** | `lwip` socket，sendto PC Server | 同上；无连接态，适合调试 |

**Serial**：发 PCM 的那一路不能混 Log/Console 文本。  
**TCP/UDP**：应用需先初始化 WiFi；PCM 与控制命令共用同一传输（TCP 全双工；UDP 同 socket 收控制）。

### 组件 API 示例（推荐）

```c
#include "esp_audio_pcm.h"

esp_audio_pcm_config_t cfg = esp_audio_pcm_config_default();
ESP_ERROR_CHECK(esp_audio_pcm_new(&cfg, &pcm));
ESP_ERROR_CHECK(esp_audio_pcm_open(pcm));   /* TCP: connect; UDP: bind+resolve */
esp_audio_pcm_write(pcm, buf, len, 20);
```

menuconfig：`Component config → ESP Audio PCM Toolkit`。

---

## 固件侧：USB Serial/JTAG（init / write）

PC 端 monitor 接收的是 ESP 经 **USB CDC（Serial/JTAG）** 发出的原始 PCM 字节流。固件侧使用 ESP-IDF 驱动 `esp_driver_usb_serial_jtag`。

### 适用芯片

ESP32-S3、ESP32-C3、ESP32-C5、ESP32-C6、ESP32-H2 等带 **USB Serial/JTAG** 外设的芯片（一根 USB 线同时用于烧录、JTAG 和 CDC 串口）。

### CMake 依赖

`main/CMakeLists.txt` 中增加：

```cmake
idf_component_register(SRCS "main.c"
                       REQUIRES esp_driver_usb_serial_jtag ...)
```

头文件：

```c
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
```

### 初始化（install）

在 `app_main()` 里、开始收发数据**之前**调用一次 `usb_serial_jtag_driver_install()`：

```c
void app_main(void)
{
    /* 发送 PCM 时 tx 缓冲建议 >= 一帧音频数据大小 */
    usb_serial_jtag_driver_config_t usb_cfg = {
        .rx_buffer_size = 256,    /* PC -> ESP 接收缓冲，仅发 PCM 可较小 */
        .tx_buffer_size = 4096,   /* ESP -> PC 发送缓冲，PCM 流建议 4096+ */
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_cfg));
}
```

也可用默认宏（缓冲各 256 字节，发 PCM 通常偏小）：

```c
usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_cfg));
```

卸载（一般很少需要）：

```c
ESP_ERROR_CHECK(usb_serial_jtag_driver_uninstall());
```

### 发送 PCM（write，本工程主要用法）

`usb_serial_jtag_write_bytes()` 把数据拷入 TX 环形缓冲，ISR 再送入 USB FIFO。返回值是**实际写入缓冲的字节数**，可能小于请求长度；返回 `-1` 且 `errno` 为 `EAGAIN`/`ENOMEM` 表示缓冲满。

**多通道示例**（4 ch，带重试）：

```c
static esp_err_t usb_send_audio(const void *data, size_t len)
{
    for (int retry = 0; retry < 8; retry++) {
        int sent = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(20));
        if (sent == (int)len) {
            return ESP_OK;
        }
        if (sent >= 0) {
            continue;  /* 部分写入，可再试或做分段发送 */
        }
        if (errno != ENOMEM && errno != EAGAIN) {
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1 + retry));
    }
    return ESP_ERR_NO_MEM;
}

/* 录音循环：codec 读一帧 -> 直接 USB 发出 */
while (!stop) {
    esp_codec_dev_read(record_dev, buffer, sizeof(buffer));
    usb_send_audio(buffer, sizeof(buffer));
}
```

**单声道示例**（带分段发送）：

```c
static esp_err_t usb_send_pcm(const void *data, size_t len)
{
    const uint8_t *ptr = data;
    size_t remaining = len;

    for (int retry = 0; retry < 16 && remaining > 0; retry++) {
        int sent = usb_serial_jtag_write_bytes((const char *)ptr, remaining,
                                               pdMS_TO_TICKS(50));
        if (sent > 0) {
            ptr += sent;
            remaining -= sent;
            retry = 0;
            continue;
        }
        if (sent < 0 && errno != EAGAIN && errno != ENOMEM) {
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1 + retry));
    }
    return remaining == 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}
```

需要确保本批数据全部到达 host 时，可在写完后调用：

```c
usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
```

### 从 PC 读取数据（read）

若需接收 PC 下发的命令或二进制数据，使用 `usb_serial_jtag_read_bytes()`：

```c
#define RX_BUF_SIZE 256

static void usb_rx_task(void *arg)
{
    uint8_t buf[RX_BUF_SIZE];

    while (1) {
        /* 阻塞等待，最多 100ms */
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n > 0) {
            /* 处理 buf[0..n-1] */
        } else if (n == 0) {
            /* 超时，无数据 */
        }
        /* n < 0 表示错误 */
    }
}

void app_main(void)
{
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 512,
        .tx_buffer_size = 4096,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    xTaskCreate(usb_rx_task, "usb_rx", 4096, NULL, 5, NULL);
}
```

回显示例（echo）：

```c
uint8_t buf[64];
int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(10));
if (n > 0) {
    usb_serial_jtag_write_bytes(buf, n, pdMS_TO_TICKS(100));
}
```

### 与 printf / Console 的关系

**原则：USB 发 PCM 期间，所有 Log 必须关闭。**

```c
/* 打开 codec、开始 USB 发 PCM 之前 */
esp_log_level_set("*", ESP_LOG_NONE);
```

| 场景 | 建议 |
|------|------|
| USB 专用于 PCM | 必须 `ESP_LOG_NONE`，禁止 `ESP_LOGx` / `printf` 走 USB |
| Console 走 UART，USB 走 PCM | Log 可打 UART，但 USB 侧仍不要有任何文本输出 |
| Console 也走 USB Serial/JTAG | 必须先关 Log，或改 Console 到 UART；**PCM 与日志不能混用同一端口** |

本 monitor 假设收到的是**纯 PCM**；一行 Log 就会破坏波形。

### PC 端对应读法

Python（`pcm_serial_bridge.py` / `usb_pcm_capture.py`）：

```python
import serial
ser = serial.Serial("COM23", baudrate=115200, timeout=0.02)
data = ser.read(4096)   # 原始 PCM 字节
```

波特率 115200 只是 CDC 默认配置，PCM 吞吐不依赖该数值；PC 按**字节流**连续读取即可。

### 数据格式约定

与 monitor 页面参数一致：

- 16 kHz 采样率
- 16 bit 有符号，小端
- 多通道交错：`ch0_s0, ch1_s0, ch2_s0, …, ch0_s1, ch1_s1, …`
- 通道数由固件决定（1–4 或更多，页面 Channels 与之对齐）

### 相关 API 速查

| API | 作用 |
|-----|------|
| `usb_serial_jtag_driver_install()` | 安装驱动，配置 TX/RX 缓冲 |
| `usb_serial_jtag_write_bytes()` | ESP → PC 发送 |
| `usb_serial_jtag_read_bytes()` | PC → ESP 接收 |
| `usb_serial_jtag_wait_tx_done()` | 等待 TX 发完 |
| `usb_serial_jtag_is_connected()` | USB 是否连上 host（收到 SOF） |
| `usb_serial_jtag_driver_uninstall()` | 卸载驱动 |

官方头文件：[usb_serial_jtag.h](https://github.com/espressif/esp-idf/blob/master/components/esp_driver_usb_serial_jtag/include/driver/usb_serial_jtag.h)

---

## 固件侧：UART（init / write）

若 PCM 走 **硬件 UART**（如 UART1 + USB-UART 转接），使用 ESP-IDF `esp_driver_uart` 驱动。

### 适用场景

- 板子无 USB Serial/JTAG，或 USB 已被 Console 占用
- 需要独立调试口：例如 **UART0 = Console/Log**，**UART1 = PCM 流**
- 需要独立调试口：例如 **UART0 = Console/Log**，**UART1 = PCM 流**（或 USB = PCM、UART = Log）

### CMake 依赖

```cmake
idf_component_register(SRCS "main.c"
                       REQUIRES esp_driver_uart ...)
```

头文件：

```c
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
```

### 初始化（install）

```c
#define PCM_UART        UART_NUM_1
#define PCM_UART_TX     GPIO_NUM_17   /* 按板级原理图修改 */
#define PCM_UART_BAUD   921600        /* 见下方波特率说明 */

#define UART_RX_BUF     256
#define UART_TX_BUF     4096

static esp_err_t pcm_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = PCM_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(PCM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(PCM_UART, PCM_UART_TX, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* queue_size=0：不用 UART 事件队列，仅 write 发 PCM */
    ESP_ERROR_CHECK(uart_driver_install(PCM_UART, UART_RX_BUF, UART_TX_BUF,
                                        0, NULL, 0));
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(pcm_uart_init());
    /* ... 启动录音任务 ... */
}
```

卸载：

```c
uart_driver_delete(PCM_UART);
```

### 波特率估算

PCM 字节率 = `sample_rate × channels × (bits/8)`

| 格式 | 字节率 | 建议波特率 |
|------|--------|-----------|
| 16 kHz / 4 ch / 16 bit | 128000 B/s | **≥ 921600**（留余量） |
| 16 kHz / 1 ch / 16 bit | 32000 B/s | 115200 可勉强，建议 460800+ |

USB CDC 下波特率几乎不影响吞吐；**UART 必须选足够高的 baud**，否则 TX 缓冲会满、丢样。

### 发送 PCM（write）

```c
static esp_err_t uart_send_pcm(const void *data, size_t len)
{
    const uint8_t *ptr = data;
    size_t remaining = len;

    for (int retry = 0; retry < 16 && remaining > 0; retry++) {
        int sent = uart_write_bytes(PCM_UART, (const char *)ptr, remaining);
        if (sent > 0) {
            ptr += sent;
            remaining -= sent;
            retry = 0;
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(1 + retry));
    }
    return remaining == 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

/* 录音循环 */
while (!stop) {
    esp_codec_dev_read(record_dev, buffer, sizeof(buffer));
    uart_send_pcm(buffer, sizeof(buffer));
}
```

等待 TX FIFO 发完（可选）：

```c
uart_wait_tx_done(PCM_UART, pdMS_TO_TICKS(100));
```

### 与 Log / Console 的关系

**PCM 和 Log 不能共用同一路 UART。**

| 布局 | 说明 |
|------|------|
| UART0 = Console，UART1 = PCM | 推荐；Log 走 UART0，PCM 走 UART1 |
| 单 UART 既 Console 又 PCM | 不可行，Log 会污染波形 |
| 发 PCM 前 | 该 UART 上 `esp_log_level_set("*", ESP_LOG_NONE)`，且不要 `printf` |

常见布局：`CONFIG_ESP_CONSOLE_UART_NUM=0`，Console/Log 在 UART0；PCM 走 USB 或另一路 UART。

### PC 端连接

monitor 工具用法相同：选 USB-UART 对应的 COM 口，波特率填与固件一致的值（如 **921600**）。

```python
import serial
ser = serial.Serial("COM5", baudrate=921600, timeout=0.02)
data = ser.read(4096)
```

### 相关 API 速查

| API | 作用 |
|-----|------|
| `uart_param_config()` | 波特率、数据位、校验等 |
| `uart_set_pin()` | TX/RX/RTS/CTS 引脚 |
| `uart_driver_install()` | 安装驱动、分配缓冲 |
| `uart_write_bytes()` | ESP → PC 发送 |
| `uart_read_bytes()` | PC → ESP 接收 |
| `uart_wait_tx_done()` | 等待 TX 发完 |
| `uart_driver_delete()` | 卸载驱动 |

官方头文件：[uart.h](https://github.com/espressif/esp-idf/blob/master/components/esp_driver_uart/include/driver/uart.h)

