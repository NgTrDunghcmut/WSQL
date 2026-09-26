#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "wifi.h"
#include "ping.h"
#include "timesync.h"
#include "record.h"
// #include "i2c_bus.h"  // OLED display - temporarily swapped for the SPI TFT
// #include "oled.h"
#include "spi_bus.h"
#include "tft.h"

#define SSID "KingSlayer"
#define PASS "24012001"
#define GATEWAY_IP "8.8.8.8"
#define SAMPLE_COUNT 20
#define SAMPLE_INTERVAL_MS 1000

void app_main() {
    printf("=== Wireless Signal Quality Logger ===\n");
    wifi_init(SSID, PASS);

    // i2c_bus_init();
    // i2c_bus_scan();
    // oled_init();
    spi_bus_init();
    tft_init();
    tft_show_welcome();
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    // Wait for connection
    for (int i = 0; i < 10; i++) {
        if (wifi_is_connected()) break;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    if (!wifi_is_connected()) {
        printf("Failed to connect to WiFi\n");
        tft_show_message("WIFI NOT CONNECTED");
        // Standby: hold here forever so execution can never fall through
        // into the sampling loop below without a real WiFi connection.
        while (1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    printf("WiFi connected, RSSI: %d dBm\n", wifi_get_rssi());
    timesync_init(10000);

    // Sample RSSI + ping latency/loss once per second, printing each
    // as a JSON line (stands in for an MQTT publish for now).
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        int64_t start_us = esp_timer_get_time();

        int rtt_ms = 0;
        bool lost = !ping_once(GATEWAY_IP, &rtt_ms);
        int rssi = wifi_get_rssi();
        // oled_render(rssi, "WIFI");
        tft_render(rssi, "WIFI");

        char json[128];
        if (record_to_json(json, sizeof(json), time(NULL), rssi, rtt_ms, lost) >= 0) {
            printf("%s\n", json);
        }

        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
        int64_t remaining_ms = SAMPLE_INTERVAL_MS - elapsed_ms;
        if (remaining_ms > 0) {
            vTaskDelay(remaining_ms / portTICK_PERIOD_MS);
        }
    }

    printf("Logging complete\n");
}
