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
#include "touch.h"
#include "ra01s.h"
#include "ble_scan.h"
#include "rolling_stats.h"
#include "pins_stats.h"

typedef struct {
    const char *label;
    const char *primary_ip;
    const char *fallback_ip; // NULL if this target has no fallback
} ping_target_t;

static const ping_target_t PING_TARGETS[] = {
    { "GOOGLE DNS",     PING_GOOGLE_PRIMARY,     PING_GOOGLE_FALLBACK },
    { "CLOUDFLARE DNS", PING_CLOUDFLARE_PRIMARY, PING_CLOUDFLARE_FALLBACK },
    { "OPENDNS",        PING_OPENDNS_PRIMARY,    NULL },
    { "LOCAL ROUTER",   PING_ROUTER_PRIMARY,     PING_ROUTER_FALLBACK },
};
#define PING_TARGET_COUNT (int)(sizeof(PING_TARGETS) / sizeof(PING_TARGETS[0]))

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

// Blocks until a ping-target row is tapped (returns its index) or the
// back button (-1).
static int wait_for_ping_target_selection(void) {
    const char *labels[PING_TARGET_COUNT];
    for (int i = 0; i < PING_TARGET_COUNT; i++) labels[i] = PING_TARGETS[i].label;

    tft_show_selection_list("SELECT PING TARGET", labels, PING_TARGET_COUNT, "NONE");
    tft_draw_back_button();

    while (1) {
        if (back_button_tapped()) return -1;

        int sx, sy;
        if (touch_read_screen_xy(&sx, &sy)) {
            int hit = tft_selection_list_hit_test(sx, sy, PING_TARGET_COUNT);
            if (hit >= 0) return hit;
            vTaskDelay(300 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }
}

// Tries up to 3 pings against ip; true if any of them got a reply.
static bool ping_target_reachable(const char *ip) {
    for (int i = 0; i < 3; i++) {
        int rtt_ms;
        if (ping_once(ip, &rtt_ms)) return true;
    }
    return false;
}

// Lets the user pick a ping target, verifies it (primary, then
// fallback if the target has one), and keeps re-showing the selection
// popup if every address for a chosen target is unreachable. Returns
// the IP to ping, or NULL if the user backed out instead.
static const char *select_and_verify_ping_target(void) {
    while (1) {
        int target_idx = wait_for_ping_target_selection();
        if (target_idx < 0) return NULL;

        const ping_target_t *target = &PING_TARGETS[target_idx];
        printf("Checking reachability of %s (%s)...\n", target->label, target->primary_ip);
        tft_show_message("CHECKING....");

        if (ping_target_reachable(target->primary_ip)) {
            return target->primary_ip;
        }
        if (target->fallback_ip != NULL && ping_target_reachable(target->fallback_ip)) {
            return target->fallback_ip;
        }

        printf("%s unreachable on all addresses\n", target->label);
        tft_show_message("TARGET UNREACHABLE");
        vTaskDelay(1500 / portTICK_PERIOD_MS);
        // loop back to the selection popup
    }
}

static void run_wifi_mode(void) {
    wifi_init(WIFI_SSID, WIFI_PASS);

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

    const char *ping_ip = select_and_verify_ping_target();
    if (ping_ip == NULL) {
        printf("Returning to main menu\n");
        return;
    }

    tft_show_wifi_select(WIFI_SSID);
    tft_draw_back_button();

    rolling_stats_t rssi_stats;
    rolling_stats_init(&rssi_stats);

    // RFC 3550 smoothed jitter over consecutive successful RTTs, kept
    // in fixed-point (scaled by 16) the way the RFC's own reference
    // code does, to avoid float math / printf("%f") entirely.
    bool have_prev_rtt = false;
    int prev_rtt_ms = 0;
    int jitter_accum_x16 = 0;

    // Sample RSSI + ping latency/loss once per second, printing each
    // as a JSON line (stands in for an MQTT publish for now), until
    // the back button is tapped.
    while (1) {
        if (back_button_tapped()) {
            printf("Returning to main menu\n");
            return;
        }

        int64_t start_us = esp_timer_get_time();

        int rtt_ms = 0;
        bool lost = !ping_once(ping_ip, &rtt_ms);
        int rssi = wifi_get_rssi();

        rolling_stats_push(&rssi_stats, rssi);
        int rssi_avg, rssi_max, rssi_min;
        rolling_stats_get(&rssi_stats, &rssi_avg, &rssi_max, &rssi_min);

        // Jitter needs two consecutive successful pings to exist; a
        // lost ping has no RTT, so it's skipped rather than treated
        // as a huge delta against the last known-good RTT.
        int jitter_ms = -1; // -1 = "not enough samples yet"
        if (!lost) {
            if (have_prev_rtt) {
                int d = rtt_ms - prev_rtt_ms;
                if (d < 0) d = -d;
                jitter_accum_x16 += d - ((jitter_accum_x16 + 8) >> 4);
                jitter_ms = jitter_accum_x16 >> 4;
            }
            prev_rtt_ms = rtt_ms;
            have_prev_rtt = true;
        }

        tft_update_wifi_stats(rssi, rtt_ms, jitter_ms, lost, rssi_avg, rssi_max, rssi_min);

        char json[160];
        if (record_to_json(json, sizeof(json), time(NULL), rssi, rtt_ms, jitter_ms, lost) >= 0) {
            printf("%s\n", json);
        }

        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
        int64_t remaining_ms = WIFI_SAMPLE_INTERVAL_MS - elapsed_ms;
        if (remaining_ms > 0) {
            vTaskDelay(remaining_ms / portTICK_PERIOD_MS);
        }
    }
}

// Beacon receiver: listens continuously, reporting RSSI/SNR of every
// received packet plus running AVG/MAX/MIN RSSI, until the back button
// is tapped.
static void run_lora_mode(void) {
    printf("Initializing LoRa radio...\n");
    LoRaInit();
    // tcxoVoltage=0.0/useRegulatorLDO=false: plain-crystal SX1262 breakout
    // (e.g. Ra-01S, no onboard TCXO) - matches the driver's own example.
    if (LoRaBegin(LORA_FREQUENCY_HZ, LORA_TX_POWER_DBM, 0.0, false) != ERR_NONE) {
        printf("LoRa radio not found (check wiring)\n");
        tft_show_message("LORA INIT FAILED");
        tft_draw_back_button();
        wait_for_back_button();
        return;
    }

    // Arms continuous RX internally (SetRx with an infinite timeout) -
    // no separate "start listening" call needed, and no re-arm needed
    // between packets (the radio stays in Rx mode automatically).
    LoRaConfig(7, SX126X_LORA_BW_125_0, SX126X_LORA_CR_4_5,
               8 /* preamble symbols */, 0 /* variable length */,
               true /* CRC on */, false /* standard IQ */);

    printf("LoRa receiver listening at %ld Hz\n", LORA_FREQUENCY_HZ);
    tft_show_lora_screen(LORA_FREQUENCY_HZ);
    tft_draw_back_button();

    rolling_stats_t rssi_stats;
    rolling_stats_init(&rssi_stats);
    int packet_count = 0;

    while (1) {
        if (back_button_tapped()) {
            printf("Returning to main menu\n");
            return;
        }

        uint8_t buf[256];
        uint8_t len = LoRaReceive(buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';

            int8_t rssi, snr;
            GetPacketStatus(&rssi, &snr); // both plain integer dB/dBm now
            packet_count++;

            rolling_stats_push(&rssi_stats, rssi);
            int rssi_avg, rssi_max, rssi_min;
            rolling_stats_get(&rssi_stats, &rssi_avg, &rssi_max, &rssi_min);

            printf("LoRa packet #%d: \"%s\" RSSI=%d SNR=%d\n",
                   packet_count, (char *)buf, rssi, snr);
            tft_update_lora_stats(rssi, snr, packet_count, rssi_avg, rssi_max, rssi_min);
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
        tft_show_selection_list("SELECT BLE DEVICE", label_ptrs, count, "SCANNING....");
        tft_draw_back_button();

        // Re-draw the list roughly once a second so newly-discovered
        // devices show up while waiting for a tap.
        int64_t refresh_until = esp_timer_get_time() + 1000000;
        while (esp_timer_get_time() < refresh_until) {
            if (back_button_tapped()) return -1;

            int sx, sy;
            if (touch_read_screen_xy(&sx, &sy)) {
                int hit = tft_selection_list_hit_test(sx, sy, count);
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

    rolling_stats_t rssi_stats;
    rolling_stats_init(&rssi_stats);

    while (1) {
        if (back_button_tapped()) {
            printf("Returning to main menu\n");
            ble_scan_disconnect();
            return;
        }

        int rssi;
        bool found = ble_scan_get_rssi(&rssi);
        if (found) {
            rolling_stats_push(&rssi_stats, rssi);
        }
        int rssi_avg = 0, rssi_max = 0, rssi_min = 0;
        rolling_stats_get(&rssi_stats, &rssi_avg, &rssi_max, &rssi_min);
        tft_update_ble_stats(found, found ? rssi : 0, rssi_avg, rssi_max, rssi_min);

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
