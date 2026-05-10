# ESP32-C3 Pro OLED 开发板

基于 **ESP32-C3**（RISC-V）的迷你开发板，板载 0.42 寸 OLED 显示屏（SSD1306，72×40 像素），支持 **WiFi** 和 **BLE 5.0**。

---

## 📋 硬件规格

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

## 🔌 引脚定义

| 引脚 | 功能 |
|------|------|
| **GPIO8** | 板载 LED |
| **GPIO6 (SCL)** | OLED I2C 时钟线 |
| **GPIO5 (SDA)** | OLED I2C 数据线 |
| **GPIO9** | BOOT 按键 |
| **GPIO7** | 复位按键（？需要确认） |

> 详见 [docs/引脚图.png](docs/引脚图.png) 和 [docs/ESP32C3 OLED原理图.pdf](docs/ESP32C3%20OLED原理图.pdf)

---

## 🛠 环境搭建

### 1. 前提条件

- **操作系统**：Windows + WSL2（Ubuntu 24.04）
- **USB 连接**：通过 [usbipd-win](https://github.com/dorssel/usbipd-win) 将开发板挂载到 WSL2

#### USB 设备挂载（WSL2）

```bash
# Windows PowerShell（管理员）
usbipd bind --busid 1-3
usbipd attach --wsl --busid 1-3
```

挂载后 WSL 中可看到设备：

```bash
ls /dev/ttyACM0
```

### 2. 安装 ESP-IDF

```bash
# 安装系统依赖
sudo apt update && sudo apt install -y ninja-build ccache libusb-1.0-0-dev python3-venv

# 克隆 ESP-IDF v5.4
mkdir -p ~/esp
cd ~/esp
git clone --depth 1 --branch release/v5.4 https://github.com/espressif/esp-idf.git

# 安装 RISC-V 工具链（ESP32-C3）
cd ~/esp/esp-idf
./install.sh esp32c3

# 添加 alias 到 ~/.bashrc
echo 'alias get_idf=". ~/esp/esp-idf/export.sh"' >> ~/.bashrc
```

### 3. 加载环境

```bash
get_idf    # 每次打开新终端后执行一次
```

### 4. 添加 dialout 权限（避免每次使用 sudo）

```bash
sudo usermod -aG dialout $USER
# 重新登录 WSL 生效
```

---

## 🔥 烧录方法

### 方法一：ESP-IDF 编译烧录

```bash
get_idf
cd esp-idf/
idf.py set-target esp32c3
idf.py menuconfig       # 配置项目
idf.py build            # 编译
idf.py -p /dev/ttyACM0 flash    # 烧录
idf.py -p /dev/ttyACM0 monitor  # 查看串口输出
```

### 方法二：Arduino 方式

Arduino 代码位于 [arduino/oled_test/](arduino/oled_test/) 目录。

使用 [Arduino IDE](https://www.arduino.cc/en/software) 或 [arduino-cli](https://arduino.github.io/arduino-cli/) 打开 `.ino` 文件，选择 **ESP32-C3** 开发板后编译烧录。

### 方法三：烧录预编译固件

资料包中已包含编译好的 `.bin` 文件：

```bash
esptool.py --chip esp32c3 -p /dev/ttyACM0 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 2MB --flash_freq 80m \
  0x0        bootloader.bin \
  0x8000     partitions.bin \
  0x10000    firmware.bin
```

---

## 📂 项目结构

```
esp32c3-oled/
├── README.md                  # 本文件
├── .gitignore
├── docs/                      # 板子技术资料
│   ├── 引脚图.png             # 引脚定义
│   ├── 尺寸图.jpg             # 板子尺寸
│   └── ESP32C3 OLED原理图.pdf # 电路原理图
├── arduino/                   # Arduino 框架代码
│   ├── oled_test/
│   │   └── oled_test.ino      # OLED 亮屏测试
│   └── README.md              # Arduino 开发说明
└── esp-idf/                   # ESP-IDF 项目
    └── blink-oled/            # LED 闪烁 + OLED 显示
        ├── main/
        │   └── blink_example_main.c  # 驱动代码（初始化、framebuffer、字体渲染）
        └── managed_components/
            └── espressif__ssd1306/   # SSD1306 组件（字体数据）
```

---

## 🚀 开发进度

- [x] 板子验证 — OLED 亮屏测试通过（Arduino U8g2）
- [x] ESP-IDF 项目搭建 — LED 闪烁 + OLED 文字显示
- [x] WiFi 连接 + OLED 显示
- [x] Web 服务器 — 手机浏览器查看系统状态
- [ ] NTP 网络时钟
- [ ] ......（持续更新）

---

## 📄 资料

- [ESP32-C3 数据手册](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_cn.pdf)
- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/)
- [SSD1306 OLED 驱动](https://github.com/olikraus/u8g2)
