#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "ble_scan.h"

#define BLE_SCAN_STALE_US (5 * 1000000) // target considered "gone" after 5s of silence

static const char *TAG = "BLEScan";
static char s_target_name[32];
static volatile int s_last_rssi = 0;
static volatile int64_t s_last_seen_us = 0;

static int ble_scan_gap_event(struct ble_gap_event *event, void *arg) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
        return 0;
    }

    size_t target_len = strlen(s_target_name);
    if (fields.name != NULL && fields.name_len == target_len &&
        memcmp(fields.name, s_target_name, target_len) == 0) {
        s_last_rssi = event->disc.rssi;
        s_last_seen_us = esp_timer_get_time();
    }

    return 0;
}

static void ble_scan_start(void) {
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d", rc);
        return;
    }

    struct ble_gap_disc_params disc_params = {0};
    disc_params.filter_duplicates = 0; // we want every advertisement, for live RSSI updates
    disc_params.passive = 0;           // active scan, so scan-response names are captured too
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, ble_scan_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error starting scan; rc=%d", rc);
    }
}

static void ble_scan_on_reset(int reason) {
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

static void ble_scan_on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ble_scan_start();
}

static void ble_scan_host_task(void *param) {
    nimble_port_run(); // returns only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

void ble_scan_init(const char *target_name) {
    strncpy(s_target_name, target_name, sizeof(s_target_name) - 1);
    s_target_name[sizeof(s_target_name) - 1] = '\0';

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return;
    }

    ble_hs_cfg.reset_cb = ble_scan_on_reset;
    ble_hs_cfg.sync_cb = ble_scan_on_sync;

    nimble_port_freertos_init(ble_scan_host_task);
}

bool ble_scan_get_rssi(int *rssi) {
    if (s_last_seen_us == 0) return false;
    if (esp_timer_get_time() - s_last_seen_us > BLE_SCAN_STALE_US) return false;
    if (rssi) *rssi = s_last_rssi;
    return true;
}
