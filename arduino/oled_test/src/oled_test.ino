#include <U8g2lib.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <time.h>

#define led 8

const char* ssid = "Huawei-2105-new";
const char* password = "W626262C";

#define BLE_SERVICE_UUID    "7a4b0001-0000-4a6e-8a5c-9e3d1f2c5b0a"
#define BLE_CHAR_UUID       "7a4b0002-0000-4a6e-8a5c-9e3d1f2c5b0a"

U8G2_SSD1306_72X40_ER_1_SW_I2C u8g2(U8G2_R0,6,5,U8X8_PIN_NONE);

NimBLEServer* bleServer = NULL;
NimBLECharacteristic* bleChar = NULL;
bool bleConnected = false;
int last_rssi = -100;

#define DBG(...) ets_printf(__VA_ARGS__)

class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s) { bleConnected = true; DBG("BLE connected\n"); }
  void onDisconnect(NimBLEServer* s) {
    bleConnected = false;
    delay(100);
    NimBLEDevice::startAdvertising();
    DBG("BLE disconnected, re-advertising\n");
  }
};

void oled_show(const char* line1, const char* line2) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, line1);
    u8g2.drawStr(0, 28, line2);
  } while (u8g2.nextPage());
}

static const char* wifi_status_str(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "Idle";
    case WL_NO_SSID_AVAIL:   return "NoAP";
    case WL_SCAN_COMPLETED:  return "ScanOK";
    case WL_CONNECT_FAILED:  return "Fail";
    case WL_CONNECTION_LOST: return "Lost";
    case WL_DISCONNECTED:    return "Disc";
    case WL_CONNECTED:       return "OK";
    default:                 return "?";
  }
}

void setup(void) {
  pinMode(led, OUTPUT);
  u8g2.setContrast(250);
  u8g2.begin();
  DBG("\n=== ESP32-C3 OLED + BLE ===\n");

  // Step 1: BLE first (must init before WiFi on ESP32-C3 for coexistence)
  DBG("Initializing NimBLE...\n");
  NimBLEDevice::init("ESP32-C3 OLED");
  NimBLEDevice::setMTU(128);  // allow longer characteristic values
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new MyServerCallbacks());
  NimBLEService* service = bleServer->createService(BLE_SERVICE_UUID);
  bleChar = service->createCharacteristic(
    BLE_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  bleChar->setValue("Starting...");
  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
  DBG("BLE advertising started\n");

  // Step 2: WiFi
  oled_show("Connecting WiFi", ssid);
  WiFi.begin(ssid, password);

  char line1[16], line2[16];
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(led, (millis() / 200) % 2);
    delay(300);
    attempts++;
    snprintf(line1, sizeof(line1), "WiFi %d/100", attempts);
    snprintf(line2, sizeof(line2), "st=%d %s", WiFi.status(), wifi_status_str(WiFi.status()));
    oled_show(line1, line2);
    DBG("WiFi %d st=%d\n", attempts, WiFi.status());
    if (attempts > 100) {
      oled_show("WiFi Failed!", ssid);
      DBG("WiFi FAILED\n");
      while (1) {
        digitalWrite(led, HIGH);
        delay(200);
        digitalWrite(led, LOW);
        delay(200);
      }
    }
  }

  digitalWrite(led, HIGH);
  DBG("WiFi OK! IP=%s RSSI=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  // NTP sync
  oled_show("Syncing NTP...", "");
  configTime(28800, 0, "pool.ntp.org", "ntp.aliyun.com");
  struct tm ti;
  int ntp_attempts = 0;
  while (!getLocalTime(&ti)) {
    delay(500);
    ntp_attempts++;
    snprintf(line1, sizeof(line1), "NTP %d/20", ntp_attempts);
    oled_show(line1, "");
    DBG("NTP wait %d\n", ntp_attempts);
    if (ntp_attempts > 20) {
      oled_show("NTP Failed!", "Will retry later");
      DBG("NTP FAILED\n");
      break;
    }
  }
  if (ntp_attempts <= 20) {
    DBG("NTP OK: %s\n", asctime(&ti));
  }

  // Re-start BLE advertising after WiFi
  NimBLEDevice::startAdvertising();
  bleChar->setValue("ESP32-C3 OLED ready");
  oled_show("WiFi+BLE OK", WiFi.localIP().toString().c_str());
  DBG("Setup complete\n");
  delay(2000);
}

void loop(void) {
  unsigned long now = millis();
  digitalWrite(led, (now / 500) % 2);

  unsigned long s = now / 1000;
  int h = s / 3600;
  int m = (s % 3600) / 60;
  int sec = s % 60;

  // WiFi/BLE coexistence: RSSI may be 0 on some reads
  int rssi = WiFi.RSSI();
  if (rssi == 0) {
    delay(1);
    rssi = WiFi.RSSI();
    if (rssi == 0) rssi = last_rssi;
    else last_rssi = rssi;
  } else {
    last_rssi = rssi;
  }

  // OLED 4-page cycle
  char line1[16], line2[16];
  switch ((now / 3000) % 4) {
    case 0:
      sprintf(line1, "ESP32-C3");
      sprintf(line2, "%s", WiFi.localIP().toString().c_str());
      break;
    case 1:
      sprintf(line1, "RSSI: %d dBm", rssi);
      sprintf(line2, "Up %02d:%02d:%02d", h, m, sec);
      break;
    case 2:
      sprintf(line1, "BLE %s", bleConnected ? "Connected" : "Standby");
      sprintf(line2, "Clients: %d", bleServer->getConnectedCount());
      break;
    case 3: {
      struct tm ti;
      if (getLocalTime(&ti)) {
        sprintf(line1, "%04d-%02d-%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        sprintf(line2, "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
      } else {
        sprintf(line1, "NTP syncing...");
        sprintf(line2, "");
      }
      break;
    }
  }

  // BLE characteristic (keep under MTU=128)
  char status[128];
  int len = snprintf(status, sizeof(status),
    "Up:%02d:%02d:%02d|RSSI:%ddBm|IP:%s",
    h, m, sec, rssi, WiFi.localIP().toString().c_str());
  bleChar->setValue(status);
  if (bleConnected && len > 0) bleChar->notify();

  // Serial debug
  static unsigned long last_log = 0;
  if (now - last_log > 3000) {
    last_log = now;
    DBG("[%02d:%02d:%02d] RSSI=%d(last=%d) BLE=%s\n",
      h, m, sec, rssi, last_rssi,
      bleConnected ? "conn" : "stdby");
  }

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, line1);
    u8g2.drawStr(0, 28, line2);
  } while (u8g2.nextPage());

  delay(500);
}
