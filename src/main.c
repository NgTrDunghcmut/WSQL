#include <stdio.h>
#include <stdint.h>
#include <limits.h>
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
#include "touch.h"

#define SSID "KingSlayer"
#define PASS "24012001"
#define GATEWAY_IP "8.8.8.8"
#define SAMPLE_COUNT 20
#define SAMPLE_INTERVAL_MS 1000

// Non-blocking: true if a touch landed on the back button just now.
static bool back_button_tapped(void) {
    int sx, sy;
    if (!touch_read_screen_xy(&sx, &sy)) return false;
    return tft_back_button_hit_test(sx, sy);
}

// Blocks until the back button is tapped.
static void wait_for_back_button(void) {
    while (!back_button_tapped()) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

// Blocks until a mode box is tapped. Returns 0=WIFI, 1=BLE, 2=LORA.
static int wait_for_mode_selection(void) {
    printf("Waiting for mode selection...\n");
    int heartbeat = 0;
    while (1) {
        int sx, sy;
        if (touch_read_screen_xy(&sx, &sy)) {
            int hit = tft_mode_select_hit_test(sx, sy);
            printf("Touch at (%d,%d) -> %s\n", sx, sy, hit >= 0 ? "hit" : "no box");
            if (hit >= 0) return hit;
            vTaskDelay(300 / portTICK_PERIOD_MS);
        } else {
            // Diagnostic heartbeat every ~2s: confirms the loop is alive
            // and shows the raw IRQ pin state even when no full touch
            // read happens (SPI read only runs while IRQ is active).
            if (++heartbeat >= 100) {
                heartbeat = 0;
                printf("Still waiting... IRQ active=%d\n", touch_irq_active());
            }
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }
}

static void run_wifi_mode(void) {
    wifi_init(SSID, PASS);

    for (int i = 0; i < 10; i++) {
        if (wifi_is_connected()) break;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    if (!wifi_is_connected()) {
        printf("Failed to connect to WiFi\n");
        tft_show_message("WIFI NOT CONNECTED");
        tft_draw_back_button();
        wait_for_back_button();
        return;
    }

    printf("WiFi connected, RSSI: %d dBm\n", wifi_get_rssi());
    timesync_init(10000);
    tft_show_wifi_select(SSID, SAMPLE_COUNT);
    tft_draw_back_button();

    long rssi_sum = 0;
    int rssi_count = 0;
    int rssi_max = INT_MIN;
    int rssi_min = INT_MAX;

    // Sample RSSI + ping latency/loss once per second, printing each
    // as a JSON line (stands in for an MQTT publish for now).
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (back_button_tapped()) {
            printf("Returning to main menu\n");
            return;
        }

        int64_t start_us = esp_timer_get_time();

        int rtt_ms = 0;
        bool lost = !ping_once(GATEWAY_IP, &rtt_ms);
        int rssi = wifi_get_rssi();

        rssi_sum += rssi;
        rssi_count++;
        if (rssi > rssi_max) rssi_max = rssi;
        if (rssi < rssi_min) rssi_min = rssi;
        int rssi_avg = (int)(rssi_sum / rssi_count);
        tft_update_wifi_stats(rssi, rssi_avg, rssi_max, rssi_min);

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
    wait_for_back_button();
}

void app_main() {
    printf("=== Wireless Signal Quality Logger ===\n");

    // i2c_bus_init();
    // i2c_bus_scan();
    // oled_init();
    spi_bus_init();
    tft_init();
    touch_init();

    tft_show_welcome();
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    while (1) {
        tft_show_mode_select();
        int mode = wait_for_mode_selection();

        if (mode == 0) {
            run_wifi_mode();
        } else {
            printf("Selected mode not implemented yet\n");
            tft_show_message("DEVELOPING");
            tft_draw_back_button();
            wait_for_back_button();
        }
    }
}
