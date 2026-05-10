/* ESP32-C3 Pro OLED - Blink + OLED Display + WiFi + Web Server
 *
 * Board: ESP32-C3 Pro OLED
 * LED: GPIO8
 * OLED: SSD1306 72x40, I2C (SDA=GPIO5, SCL=GPIO6)
 */
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "ssd1306_fonts.h"

// WiFi credentials
#define WIFI_SSID       "Huawei-2105-new"
#define WIFI_PASS       "W626262C"
#define WIFI_MAX_RETRY  3

// Pin definitions
#define BLINK_GPIO     8
#define I2C_SCL_IO     6
#define I2C_SDA_IO     5
#define I2C_PORT       I2C_NUM_0
#define I2C_FREQ_HZ    400000
#define OLED_ADDR      0x3C

// OLED dimensions (72x40) within SSD1306 128x64 address space
#define OLED_WIDTH     72
#define OLED_HEIGHT    40
#define OLED_PAGES     ((OLED_HEIGHT + 7) / 8)  // 5 pages
#define COL_OFFSET     28  // ER 72x40 glass starts at SEG 28 within 128-wide GDDRAM

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
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, c, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    if (ret != ESP_OK) ESP_LOGE(TAG, "I2C cmd 0x%02x failed: %s", cmd, esp_err_to_name(ret));
}

static void oled_write_data_bulk(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, OLED_ADDR << 1 | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, 0x40, true);  // Co=0, D/C#=1 (data)
    i2c_master_write(c, data, len, true);
    i2c_master_stop(c);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, c, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(c);
    if (ret != ESP_OK) ESP_LOGE(TAG, "I2C data write (%zu bytes) failed: %s", len, esp_err_to_name(ret));
}

// ── OLED init for 72x40 SSD1306 (U8g2 match) ─────────────

static void oled_init(void)
{
    oled_write_cmd(0xAE);  // display off
    oled_write_cmd(0xD5);  // clock divide ratio
    oled_write_cmd(0x80);
    oled_write_cmd(0xA8);  // multiplex ratio
    oled_write_cmd(0x27);  // 40 rows - 1 = 39 (0x27)
    oled_write_cmd(0xD3);  // display offset
    oled_write_cmd(0x00);
    oled_write_cmd(0xAD);  // internal IREF setting
    oled_write_cmd(0x30);
    oled_write_cmd(0x8D);  // charge pump
    oled_write_cmd(0x14);  // enable
    oled_write_cmd(0x40);  // display start line = 0
    oled_write_cmd(0xA6);  // normal (non-inverted)
    oled_write_cmd(0xA4);  // output RAM to display
    oled_write_cmd(0x20);  // memory addressing mode
    oled_write_cmd(0x01);  // vertical mode (matches framebuffer layout)
    oled_write_cmd(0xA1);  // SEG remap (col 127 → SEG0, correct for ER module)
    oled_write_cmd(0xC8);  // COM scan remapped
    oled_write_cmd(0xDA);  // COM pins config
    oled_write_cmd(0x12);  // alternative config
    oled_write_cmd(0x81);  // contrast
    oled_write_cmd(0xAF);
    oled_write_cmd(0xD9);  // pre-charge
    oled_write_cmd(0x22);
    oled_write_cmd(0xDB);  // vcomh
    oled_write_cmd(0x20);
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
    // Set column range with offset to align with glass
    oled_write_cmd(0x21);  // set column address
    oled_write_cmd(COL_OFFSET);
    oled_write_cmd(COL_OFFSET + OLED_WIDTH - 1);

    // Set page range
    oled_write_cmd(0x22);  // set page address
    oled_write_cmd(0);
    oled_write_cmd(OLED_PAGES - 1);

    // Send whole framebuffer in one shot (vertical mode: col0_p0..p4, col1_p0..p4, ...)
    oled_write_data_bulk(&framebuffer[0][0], sizeof(framebuffer));
}

// ── Text rendering (font1206: 6 cols x 12 rows, byte-pair per column) ──

static void oled_draw_char(uint8_t x, uint8_t y, char c)
{
    if (c < ' ' || c > '~') c = ' ';
    c -= ' ';
    uint8_t y0 = y;
    for (int i = 0; i < 12; i++) {          // 12 font bytes = 6 byte-pairs
        uint8_t chTemp = c_chFont1206[(uint8_t)c][i];
        for (int j = 0; j < 8; j++) {       // 8 bits per byte, MSB first
            if (chTemp & 0x80) {
                oled_draw_pixel(x, y, 1);
            }
            chTemp <<= 1;
            y++;
            if ((y - y0) == 12) {           // column height = 12
                y = y0;
                x++;
                break;
            }
        }
    }
}

static void oled_draw_string(uint8_t x, uint8_t y, const char *str, uint8_t max_len)
{
    uint8_t start_x = x;
    while (*str && max_len--) {
        oled_draw_char(x, y, *str);
        x += 6;
        if (x + 6 > start_x + OLED_WIDTH) {
            x = start_x;
            y += 12;
        }
        str++;
    }
}

// ── WiFi ─────────────────────────────────────────────────────

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t wifi_event_group;
static int wifi_retry_count = 0;
static char wifi_ip[16] = "0.0.0.0";

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count < WIFI_MAX_RETRY) {
            wifi_retry_count++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        esp_ip4addr_ntoa(&event->ip_info.ip, wifi_ip, sizeof(wifi_ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                       ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                       IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi connecting to %s...", WIFI_SSID);
}

// ── Web Server ───────────────────────────────────────────────

static int get_wifi_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        return ap.rssi;
    return 0;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char resp[2048];
    uint64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_s = (uint32_t)(uptime_us / 1000000);
    int rssi = get_wifi_rssi();
    uint32_t free_heap = esp_get_free_heap_size();

    // Get internal temperature
    float temp = 0;
    temperature_sensor_handle_t temp_sensor = NULL;
    temperature_sensor_config_t tsens = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tsens, &temp_sensor) == ESP_OK
        && temperature_sensor_enable(temp_sensor) == ESP_OK
        && temperature_sensor_get_celsius(temp_sensor, &temp) == ESP_OK) {
        temperature_sensor_disable(temp_sensor);
        temperature_sensor_uninstall(temp_sensor);
    }

    snprintf(resp, sizeof(resp),
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='5'>"
        "<title>ESP32-C3 OLED</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,sans-serif}"
        "body{padding:20px;background:#1a1a2e;color:#eee}"
        "h1{font-size:22px;margin-bottom:16px;color:#e94560}"
        ".card{background:#16213e;border-radius:12px;padding:16px;margin-bottom:12px}"
        ".card h2{font-size:14px;color:#888;margin-bottom:4px}"
        ".card .val{font-size:28px;font-weight:700}"
        ".row{display:flex;gap:12px}"
        ".row .card{flex:1}"
        ".rssi-bar{height:6px;border-radius:3px;background:#333;margin-top:8px;overflow:hidden}"
        ".rssi-bar div{height:100%%;border-radius:3px;background:#e94560}"
        ".ip{color:#0f3460;font-size:13px;text-align:center;margin-top:16px}"
        "</style></head><body>"
        "<h1>ESP32-C3 OLED</h1>"
        "<div class='row'>"
        "<div class='card'><h2>Uptime</h2><div class='val'>%02u:%02u:%02u</div></div>"
        "<div class='card'><h2>Temp</h2><div class='val'>%.1f&deg;C</div></div>"
        "</div>"
        "<div class='card'><h2>WiFi RSSI</h2><div class='val'>%d dBm</div>"
        "<div class='rssi-bar'><div style='width:%d%%'></div></div></div>"
        "<div class='card'><h2>Free Heap</h2><div class='val'>%lu KB</div></div>"
        "<div class='ip'>%s</div>"
        "</body></html>",
        (unsigned)(uptime_s / 3600), (unsigned)((uptime_s % 3600) / 60), (unsigned)(uptime_s % 60),
        temp, rssi, (rssi > -30) ? 100 : (rssi > -67) ? 80 : (rssi > -80) ? 60 : (rssi > -90) ? 40 : 20,
        free_heap / 1024, wifi_ip);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
};

static void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.lru_purge_enable = true;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        ESP_LOGI(TAG, "Web server started: http://%s:8080", wifi_ip);
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
    oled_draw_string(1, 4, "Connecting...", 11);
    oled_refresh();

    // Initialize NVS (needed by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Connect to WiFi
    wifi_init_sta();

    // Wait for WiFi (blocks until connected or retries exhausted)
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    oled_clear();
    if (strcmp(wifi_ip, "0.0.0.0") != 0) {
        oled_draw_string(1, 4, "zhanghouxin", 11);
        oled_draw_string(1, 16, "WiFi OK", 11);
        oled_draw_string(1, 28, wifi_ip, 11);
        ESP_LOGI(TAG, "WiFi connected, IP: %s", wifi_ip);
        start_webserver();
    } else {
        oled_draw_string(1, 4, "WiFi FAIL", 11);
        oled_draw_string(1, 16, "Check", 11);
        oled_draw_string(1, 28, "credentials", 11);
        ESP_LOGE(TAG, "WiFi connection failed");
    }
    oled_refresh();

    // Blink loop
    bool state = false;
    while (1) {
        state = !state;
        gpio_set_level(BLINK_GPIO, state);
        ESP_LOGI(TAG, "LED %s", state ? "ON" : "OFF");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
