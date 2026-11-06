#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "ble_scan.h"

#define BLE_SCAN_STALE_US (5 * 1000000) // device considered "gone" after 5s of silence
#define BLE_SCAN_NAME_LEN 32

typedef struct {
    ble_addr_t addr;
    char name[BLE_SCAN_NAME_LEN];
    int rssi;
    int64_t last_seen_us;
} ble_scan_device_t;

static const char *TAG = "BLEScan";
static ble_scan_device_t s_devices[BLE_SCAN_MAX_DEVICES];
static int s_device_count = 0;
static int s_selected_index = -1;

static int find_device(const ble_addr_t *addr) {
    for (int i = 0; i < s_device_count; i++) {
        if (memcmp(&s_devices[i].addr, addr, sizeof(ble_addr_t)) == 0) return i;
    }
    return -1;
}

static int ble_scan_gap_event(struct ble_gap_event *event, void *arg) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
        return 0;
    }

    int idx = find_device(&event->disc.addr);
    if (idx < 0) {
        if (s_device_count >= BLE_SCAN_MAX_DEVICES) return 0; // list full, ignore new devices
        idx = s_device_count++;
        s_devices[idx].addr = event->disc.addr;
        strcpy(s_devices[idx].name, "(no name)");
    }

    s_devices[idx].rssi = event->disc.rssi;
    s_devices[idx].last_seen_us = esp_timer_get_time();

    if (fields.name != NULL && fields.name_len > 0) {
        size_t n = fields.name_len;
        if (n > BLE_SCAN_NAME_LEN - 1) n = BLE_SCAN_NAME_LEN - 1;
        for (size_t i = 0; i < n; i++) {
            // Our display font is uppercase-only - fold names to match.
            s_devices[idx].name[i] = (char)toupper((unsigned char)fields.name[i]);
        }
        s_devices[idx].name[n] = '\0';
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

void ble_scan_init(void) {
    s_device_count = 0;
    s_selected_index = -1;

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

int ble_scan_get_device_count(void) {
    return s_device_count;
}

bool ble_scan_get_device_name(int index, char *buf, size_t buf_size) {
    if (index < 0 || index >= s_device_count) return false;
    snprintf(buf, buf_size, "%s", s_devices[index].name);
    return true;
}

bool ble_scan_get_device_label(int index, char *buf, size_t buf_size) {
    if (index < 0 || index >= s_device_count) return false;
    snprintf(buf, buf_size, "%s RSSI:%d", s_devices[index].name, s_devices[index].rssi);
    return true;
}

void ble_scan_select_device(int index) {
    if (index < 0 || index >= s_device_count) return;
    s_selected_index = index;
}

bool ble_scan_get_rssi(int *rssi) {
    if (s_selected_index < 0 || s_selected_index >= s_device_count) return false;
    ble_scan_device_t *d = &s_devices[s_selected_index];
    if (esp_timer_get_time() - d->last_seen_us > BLE_SCAN_STALE_US) return false;
    if (rssi) *rssi = d->rssi;
    return true;
}
