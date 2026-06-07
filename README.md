# ESP32-C3 Pro OLED 开发板

基于 **ESP32-C3**（RISC-V）的迷你开发板，板载 0.42 寸 OLED 显示屏（SSD1306，72×40 像素），支持 **WiFi** 和 **BLE 5.0**。

---

## 硬件规格

| 参数 | 规格 |
|------|------|
| **芯片** | ESP32-C3 (QFN32) revision v0.4 |
| **CPU** | 单核 RISC-V @ 160MHz |
| **Flash** | 4MB |
| **SRAM** | 400KB |
| **WiFi** | 802.11 b/g/n |
| **蓝牙** | BLE 5.0 |
| **USB** | USB-Serial/JTAG 内置（无需外接调试器） |
| **OLED** | SSD1306 72×40 像素（I2C） |
| **板载 LED** | GPIO8 |

## 引脚定义

| 引脚 | 功能 |
|------|------|
| **GPIO8** | 板载 LED |
| **GPIO6 (SCL)** | OLED I2C 时钟线 |
| **GPIO5 (SDA)** | OLED I2C 数据线 |
| **GPIO9** | BOOT 按键 |

> 详见 [docs/引脚图.png](docs/引脚图.png) 和 [docs/ESP32C3 OLED原理图.pdf](docs/ESP32C3%20OLED原理图.pdf)

---

## 固件功能

当前固件 (`arduino/oled_test/`) 实现：

- **WiFi 连接** — 启动时 OLED 显示连接进度和状态码
- **BLE 蓝牙服务** — 广播设备名 "ESP32-C3 OLED"，可通过 nRF Connect 等工具连接
- **实时状态显示** — OLED 四页循环：
  - 设备名 + IP 地址
  - WiFi 信号强度 (RSSI) + 运行时间
  - BLE 连接状态 + 客户端数
  - NTP 日期 + 时间（北京时间）
- **NTP 网络时钟** — 启动时自动同步，失败不阻塞，OLED 显示实时日期时间
- **BLE 特征值** — UUID `7a4b0002`，包含运行时间、RSSI、IP 地址，支持 READ/NOTIFY

---

## 环境搭建

### 方案一：PlatformIO（推荐）

安装 [VS Code](https://code.visualstudio.com/) + [PlatformIO 扩展](https://platformio.org/)，直接打开 `arduino/oled_test/` 目录。

```bash
# 编译
pio run

# 烧录
pio run -t upload --upload-port COM3

# 查看串口输出（USB-Serial/JTAG 使用 ets_printf）
pio device monitor --port COM3 --baud 115200
```

### 方案二：ESP-IDF

```bash
get_idf
cd esp-idf/blink-oled/
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### 方案三：Arduino IDE

使用 [Arduino IDE](https://www.arduino.cc/en/software) 打开 `.ino` 文件，安装 **ESP32** 开发板支持包和 **U8g2** 库，选择 **ESP32-C3** 开发板后编译烧录。

---

## 项目结构

```
esp32c3-oled/
├── README.md                  # 本文件
├── .gitignore
├── docs/                      # 板子技术资料
│   ├── 引脚图.png
│   ├── 尺寸图.jpg
│   └── ESP32C3 OLED原理图.pdf
├── arduino/                   # Arduino 框架代码
│   ├── oled_test/
│   │   ├── platformio.ini     # PlatformIO 配置
│   │   └── src/
│   │       └── oled_test.ino  # 主程序（WiFi + BLE + OLED）
│   └── README.md
└── esp-idf/                   # ESP-IDF 项目
    └── blink-oled/            # LED 闪烁 + OLED 显示
        ├── main/
        │   └── blink_example_main.c
        └── managed_components/
```

---

## BLE 调试

使用 nRF Connect 或任意 BLE 调试工具：

1. 扫描 → 找到 **ESP32-C3 OLED**
2. 连接 → Service UUID `7a4b0001`
3. 读取/订阅 Characteristic UUID `7a4b0002`

返回数据格式：
```
Up:00:05:23|RSSI:-67dBm|IP:192.168.3.34
```

---

## 开发进度

- [x] OLED 亮屏测试（Arduino U8g2）
- [x] ESP-IDF 项目 — LED 闪烁 + OLED 文字
- [x] WiFi 连接 + OLED 状态显示
- [x] BLE 蓝牙服务（NimBLE）
- [x] WiFi 信号强度 RSSI 实时监控
- [x] NTP 网络时钟

---

## 资料

- [ESP32-C3 数据手册](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_cn.pdf)
- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/)
- [SSD1306 OLED 驱动 (U8g2)](https://github.com/olikraus/u8g2)
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
