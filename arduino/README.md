# Arduino 开发

## 环境要求

- [Arduino IDE](https://www.arduino.cc/en/software) 或 [arduino-cli](https://arduino.github.io/arduino-cli/)
- [ESP32 Arduino 支持](https://github.com/espressif/arduino-esp32)

## oled_test

亮屏测试程序：
- OLED 显示 "ESP32-C3"
- 板载 LED 以 0.5 秒间隔闪烁

### 接线

| OLED | ESP32-C3 |
|------|----------|
| SCL | GPIO6 |
| SDA | GPIO5 |
| VCC | 3.3V |
| GND | GND |

### 烧录

Arduino IDE 中选择：
- **开发板**: ESP32C3 Dev Module
- **端口**: /dev/ttyACM0（WSL2）或 COM3（Windows）
