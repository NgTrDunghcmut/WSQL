#ifndef PINS_STATS_H
#define PINS_STATS_H

// Central place for every GPIO assignment and every user-tunable
// sampling/statistics constant in this project. Change a value here
// and every module that uses it picks it up.

// ---- I2C bus (OLED display) - currently disabled in main.c ----
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// ---- SPI bus (TFT display) ----
#define SPI_SCK_PIN  5
#define SPI_MOSI_PIN 18
#define SPI_MISO_PIN 22

// ---- TFT control lines (CS is passed to the SPI driver, not toggled manually) ----
#define TFT_CS_PIN  23
#define TFT_DC_PIN  19
#define TFT_RST_PIN 21

// ---- XPT2046 touch controller (shares SCK/MOSI/MISO with the TFT's SPI bus) ----
#define TOUCH_CS_PIN  32
#define TOUCH_IRQ_PIN 35

// ---- Remote API endpoint ----
// URL that api_post_send() POSTs sample data to. Fill in before use.
#define API_POST_URL "http://example.com/api/samples"

// ---- WiFi mode ----
#define WIFI_SSID "KingSlayer"
#define WIFI_PASS "24012001"
#define WIFI_SAMPLE_INTERVAL_MS 1000 // time between RSSI/ping samples

// ---- WiFi ping targets, selectable via the "SELECT PING TARGET" popup ----
// Each target tries its primary IP first; if that's unreachable, the
// fallback IP (leave as NULL in main.c's table if there isn't one) is
// tried next. If every address fails, the popup reappears.
#define PING_GOOGLE_PRIMARY      "8.8.8.8"
#define PING_GOOGLE_FALLBACK     "8.8.4.4"
#define PING_CLOUDFLARE_PRIMARY  "1.1.1.1"
#define PING_CLOUDFLARE_FALLBACK "1.0.0.1"
#define PING_OPENDNS_PRIMARY     "208.67.222.222"
#define PING_ROUTER_PRIMARY      "192.168.1.1"
#define PING_ROUTER_FALLBACK     "192.168.0.1"

// ---- LoRa mode (SX1262) ----
#define LORA_FREQUENCY_HZ 433000000L
#define LORA_TX_POWER_DBM 22 // SX1262 max; different envelope than the old SX1276 (-3..+22 dBm)

// ---- Rolling AVG/MAX/MIN window (shared by every mode, see rolling_stats.h) ----
#define ROLLING_STATS_CAP 100

#endif
