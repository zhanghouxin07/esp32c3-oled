/* ESP32-C3 Pro OLED - Blink + OLED Display
 *
 * Board: ESP32-C3 Pro OLED
 * LED: GPIO8
 * OLED: SSD1306 72x40, I2C (SDA=GPIO5, SCL=GPIO6)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "ssd1306_fonts.h"

// Pin definitions
#define BLINK_GPIO     8
#define I2C_SCL_IO     6
#define I2C_SDA_IO     5
#define I2C_PORT       I2C_NUM_0
#define I2C_FREQ_HZ    400000
#define OLED_ADDR      0x3C

// OLED dimensions (72x40)
#define OLED_WIDTH     72
#define OLED_HEIGHT    40
#define OLED_PAGES     ((OLED_HEIGHT + 7) / 8)  // 5 pages

static const char *TAG = "esp32c3-oled";

// Framebuffer: 72 columns x 5 pages (72 * 5 = 360 bytes)
static uint8_t framebuffer[OLED_WIDTH][OLED_PAGES];

// ── I2C helpers ──────────────────────────────────────────

static void oled_write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, OLED_ADDR << 1 | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, 0x00, true);  // Co=0, D/C#=0 (command)
    i2c_master_write_byte(c, cmd, true);
    i2c_master_stop(c);
    i2c_master_cmd_begin(I2C_PORT, c, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
}

static void oled_write_data_bulk(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, OLED_ADDR << 1 | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, 0x40, true);  // Co=0, D/C#=1 (data)
    i2c_master_write(c, data, len, true);
    i2c_master_stop(c);
    i2c_master_cmd_begin(I2C_PORT, c, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
}

// ── OLED init for 72x40 SSD1306 ──────────────────────────

static void oled_init(void)
{
    oled_write_cmd(0xAE);  // display off
    oled_write_cmd(0xD5);  // clock divide ratio
    oled_write_cmd(0x80);
    oled_write_cmd(0xA8);  // multiplex ratio
    oled_write_cmd(0x27);  // 40 rows - 1 = 39 (0x27)
    oled_write_cmd(0xD3);  // display offset
    oled_write_cmd(0x00);
    oled_write_cmd(0x40);  // display start line
    oled_write_cmd(0x8D);  // charge pump
    oled_write_cmd(0x14);  // enable
    oled_write_cmd(0x20);  // memory addressing mode
    oled_write_cmd(0x01);  // vertical addressing mode
    oled_write_cmd(0xA1);  // segment remap (column 127 mapped to SEG0)
    oled_write_cmd(0xC8);  // COM scan direction (remapped)
    oled_write_cmd(0xDA);  // COM pins config
    oled_write_cmd(0x12);  // alternative config
    oled_write_cmd(0x81);  // contrast
    oled_write_cmd(0xCF);
    oled_write_cmd(0xD9);  // pre-charge
    oled_write_cmd(0xF1);
    oled_write_cmd(0xDB);  // vcomh
    oled_write_cmd(0x40);
    oled_write_cmd(0xA4);  // display all on resume
    oled_write_cmd(0xA6);  // normal (non-inverted)
    oled_write_cmd(0x2E);  // deactivate scroll
    oled_write_cmd(0xAF);  // display on
}

// ── Framebuffer operations ───────────────────────────────

static void oled_clear(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
}

static void oled_draw_pixel(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    uint8_t page = y >> 3;       // y / 8
    uint8_t bit  = y & 0x07;      // y % 8
    if (color)
        framebuffer[x][page] |= (1 << bit);
    else
        framebuffer[x][page] &= ~(1 << bit);
}

static void oled_refresh(void)
{
    // Set column range: 0 .. 71
    oled_write_cmd(0x21);  // set column address
    oled_write_cmd(0);
    oled_write_cmd(OLED_WIDTH - 1);

    // Set page range: 0 .. 4
    oled_write_cmd(0x22);  // set page address
    oled_write_cmd(0);
    oled_write_cmd(OLED_PAGES - 1);

    // Send framebuffer
    for (int page = 0; page < OLED_PAGES; page++) {
        oled_write_data_bulk(&framebuffer[0][page], OLED_WIDTH);
    }
}

// ── Text rendering (uses 1206 font from managed component) ──

static void oled_draw_char(uint8_t x, uint8_t y, char c)
{
    if (c < ' ' || c > '~') c = ' ';
    c -= ' ';
    for (int col = 0; col < 12; col++) {   // font1206 is 12 columns wide
        uint8_t line = c_chFont1206[(uint8_t)c][col];
        for (int row = 0; row < 8; row++) {
            if (line & (1 << (7 - row))) {
                oled_draw_pixel(x + col, y + row, 1);
            }
        }
    }
}

static void oled_draw_string(uint8_t x, uint8_t y, const char *str)
{
    while (*str) {
        oled_draw_char(x, y, *str);
        x += 7;  // 6 + 1 spacing
        if (x + 12 > OLED_WIDTH) {
            x = 0;
            y += 10;
        }
        str++;
    }
}

// ── Main ─────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32-C3 Pro OLED");

    // Configure LED GPIO
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    // Configure I2C master
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0));

    // Initialize OLED
    oled_init();
    oled_clear();
    oled_draw_string(1, 4, "ESP32-C3");
    oled_draw_string(1, 16, "OLED 72x40");
    oled_draw_string(1, 28, "Hello!");
    oled_refresh();

    ESP_LOGI(TAG, "OLED initialized, LED blink loop started");

    // Blink loop
    bool state = false;
    while (1) {
        state = !state;
        gpio_set_level(BLINK_GPIO, state);
        ESP_LOGI(TAG, "LED %s", state ? "ON" : "OFF");
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
