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
#include "lora.h"
#include "ble_scan.h"

#define SSID "KingSlayer"
#define PASS "24012001"
#define GATEWAY_IP "8.8.8.8"
#define SAMPLE_COUNT 20
#define SAMPLE_INTERVAL_MS 1000
#define LORA_FREQUENCY_HZ 433000000L

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

// Beacon receiver: listens continuously, reporting RSSI/SNR of every
// received packet plus running AVG/MAX/MIN RSSI, until the back button
// is tapped.
static void run_lora_mode(void) {
    printf("Initializing LoRa radio...\n");
    if (!lora_init()) {
        printf("LoRa radio not found (check wiring)\n");
        tft_show_message("LORA INIT FAILED");
        tft_draw_back_button();
        wait_for_back_button();
        return;
    }

    lora_set_frequency(LORA_FREQUENCY_HZ);
    lora_set_bandwidth(7);        // 125 kHz
    lora_set_coding_rate(1);      // 4/5
    lora_set_spreading_factor(7);
    lora_enable_crc();
    lora_receive();

    printf("LoRa receiver listening at %ld Hz\n", LORA_FREQUENCY_HZ);
    tft_show_lora_screen(LORA_FREQUENCY_HZ);
    tft_draw_back_button();

    long rssi_sum = 0;
    int rssi_count = 0;
    int rssi_max = INT_MIN;
    int rssi_min = INT_MAX;
    int packet_count = 0;

    while (1) {
        if (back_button_tapped()) {
            printf("Returning to main menu\n");
            return;
        }

        if (lora_received()) {
            uint8_t buf[256];
            int len = lora_receive_packet(buf, sizeof(buf) - 1);
            buf[len] = '\0';

            int rssi = lora_packet_rssi();
            float snr = lora_packet_snr();
            packet_count++;

            rssi_sum += rssi;
            rssi_count++;
            if (rssi > rssi_max) rssi_max = rssi;
            if (rssi < rssi_min) rssi_min = rssi;
            int rssi_avg = (int)(rssi_sum / rssi_count);

            int snr_tenths = (int)(snr * 10.0f);
            bool snr_negative = snr_tenths < 0;
            if (snr_negative) snr_tenths = -snr_tenths;
            printf("LoRa packet #%d: \"%s\" RSSI=%d SNR=%s%d.%d\n",
                   packet_count, (char *)buf, rssi,
                   snr_negative ? "-" : "", snr_tenths / 10, snr_tenths % 10);
            tft_update_lora_stats(rssi, snr, packet_count, rssi_avg, rssi_max, rssi_min);

            lora_receive(); // resume listening after reading the packet
        }

        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

// Shows the live-updating scan-result list (named devices only) and
// blocks until the user taps a device row (returns its index) or the
// back button (-1).
static int wait_for_ble_device_selection(void) {
    while (1) {
        int count = ble_scan_get_device_count();
        char labels[BLE_SCAN_MAX_DEVICES][BLE_SCAN_NAME_LEN];
        const char *label_ptrs[BLE_SCAN_MAX_DEVICES];
        for (int i = 0; i < count; i++) {
            ble_scan_get_device_name(i, labels[i], sizeof(labels[i]));
            label_ptrs[i] = labels[i];
        }
        tft_show_ble_scan_list(label_ptrs, count);
        tft_draw_back_button();

        // Re-draw the list roughly once a second so newly-discovered
        // devices show up while waiting for a tap.
        int64_t refresh_until = esp_timer_get_time() + 1000000;
        while (esp_timer_get_time() < refresh_until) {
            if (back_button_tapped()) return -1;

            int sx, sy;
            if (touch_read_screen_xy(&sx, &sy)) {
                int hit = tft_ble_scan_list_hit_test(sx, sy, count);
                if (hit >= 0) return hit;
                vTaskDelay(300 / portTICK_PERIOD_MS);
            } else {
                vTaskDelay(20 / portTICK_PERIOD_MS);
            }
        }
    }
}

// Blocks until the pending connect attempt resolves, or the back
// button is tapped (which aborts by just returning - the connect
// attempt may still complete in the background, in which case the
// next ble_scan_init() restarts discovery and drops it).
static ble_connect_result_t wait_for_ble_connect(void) {
    ble_connect_result_t result;
    while ((result = ble_scan_get_connect_result()) == BLE_CONNECT_PENDING) {
        if (back_button_tapped()) return BLE_CONNECT_NONE;
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    return result;
}

// Scans for nearby named BLE devices, lets the user pick one from a
// live list, connects to it, then tracks the connection's live RSSI
// (plus running AVG/MAX/MIN) until the back button is tapped.
static void run_ble_mode(void) {
    printf("Starting BLE scan...\n");
    ble_scan_init();

    int selected = wait_for_ble_device_selection();
    if (selected < 0) {
        printf("Returning to main menu\n");
        return;
    }

    char target_name[BLE_SCAN_NAME_LEN];
    ble_scan_get_device_name(selected, target_name, sizeof(target_name));

    printf("Connecting to %s...\n", target_name);
    tft_show_message("CONNECTING....");
    ble_scan_connect_device(selected);

    ble_connect_result_t result = wait_for_ble_connect();
    if (result == BLE_CONNECT_NONE) {
        printf("Returning to main menu\n");
        return;
    }
    if (result != BLE_CONNECT_SUCCESS) {
        printf("Failed to connect to %s\n", target_name);
        tft_show_message("CONNECT FAILED");
        tft_draw_back_button();
        wait_for_back_button();
        return;
    }

    printf("Connected to %s\n", target_name);
    tft_show_ble_screen(target_name);
    tft_draw_back_button();

    long rssi_sum = 0;
    int rssi_count = 0;
    int rssi_max = INT_MIN;
    int rssi_min = INT_MAX;

    while (1) {
        if (back_button_tapped()) {
            printf("Returning to main menu\n");
            ble_scan_disconnect();
            return;
        }

        int rssi;
        bool found = ble_scan_get_rssi(&rssi);
        if (found) {
            rssi_sum += rssi;
            rssi_count++;
            if (rssi > rssi_max) rssi_max = rssi;
            if (rssi < rssi_min) rssi_min = rssi;
        }
        int rssi_avg = (rssi_count > 0) ? (int)(rssi_sum / rssi_count) : 0;
        tft_update_ble_stats(found, found ? rssi : 0,
                              rssi_avg, rssi_count > 0 ? rssi_max : 0, rssi_count > 0 ? rssi_min : 0);

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
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
        } else if (mode == 1) {
            run_ble_mode();
        } else if (mode == 2) {
            run_lora_mode();
        }
    }
}
