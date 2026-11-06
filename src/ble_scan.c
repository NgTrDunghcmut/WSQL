#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/ble.h" // BLE_ERR_REM_USER_CONN_TERM
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "ble_scan.h"

#define BLE_CONNECT_TIMEOUT_MS 10000

typedef struct {
    ble_addr_t addr;
    char name[BLE_SCAN_NAME_LEN];
} ble_scan_device_t;

static const char *TAG = "BLEScan";
static bool s_nimble_started = false;

static ble_scan_device_t s_devices[BLE_SCAN_MAX_DEVICES];
static int s_device_count = 0;

static volatile ble_connect_result_t s_connect_result = BLE_CONNECT_NONE;
static volatile uint16_t s_conn_handle = 0;
static volatile bool s_connected = false;

static int find_device(const ble_addr_t *addr) {
    for (int i = 0; i < s_device_count; i++) {
        if (memcmp(&s_devices[i].addr, addr, sizeof(ble_addr_t)) == 0) return i;
    }
    return -1;
}

static int ble_scan_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }
        // Ignore anything that doesn't advertise a name at all.
        if (fields.name == NULL || fields.name_len == 0) return 0;

        int idx = find_device(&event->disc.addr);
        if (idx < 0) {
            if (s_device_count >= BLE_SCAN_MAX_DEVICES) return 0; // list full
            idx = s_device_count++;
            s_devices[idx].addr = event->disc.addr;
        }

        size_t n = fields.name_len;
        if (n > BLE_SCAN_NAME_LEN - 1) n = BLE_SCAN_NAME_LEN - 1;
        for (size_t i = 0; i < n; i++) {
            // Our display font is uppercase-only - fold names to match.
            s_devices[idx].name[i] = (char)toupper((unsigned char)fields.name[i]);
        }
        s_devices[idx].name[n] = '\0';
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_connected = true;
            s_connect_result = BLE_CONNECT_SUCCESS;
            ESP_LOGI(TAG, "Connected, conn_handle=%d", s_conn_handle);
        } else {
            s_connected = false;
            s_connect_result = BLE_CONNECT_FAILED;
            ESP_LOGE(TAG, "Connect failed; status=%d", event->connect.status);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        s_connected = false;
        return 0;

    default:
        return 0;
    }
}

static void ble_scan_start_discovery(void) {
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
    ble_scan_start_discovery();
}

static void ble_scan_host_task(void *param) {
    nimble_port_run(); // returns only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

void ble_scan_init(void) {
    s_device_count = 0;
    s_connect_result = BLE_CONNECT_NONE;
    s_connected = false;

    if (s_nimble_started) {
        // NimBLE host is already running from a previous BLE-mode
        // session - just restart discovery instead of re-initializing
        // the whole stack (nimble_port_init() isn't meant to be called
        // twice without a matching deinit).
        ble_scan_start_discovery();
        return;
    }

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
    s_nimble_started = true;
}

int ble_scan_get_device_count(void) {
    return s_device_count;
}

bool ble_scan_get_device_name(int index, char *buf, size_t buf_size) {
    if (index < 0 || index >= s_device_count) return false;
    snprintf(buf, buf_size, "%s", s_devices[index].name);
    return true;
}

void ble_scan_connect_device(int index) {
    if (index < 0 || index >= s_device_count) return;

    s_connect_result = BLE_CONNECT_PENDING;
    ble_gap_disc_cancel(); // must stop discovery before connecting

    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d", rc);
        s_connect_result = BLE_CONNECT_FAILED;
        return;
    }

    rc = ble_gap_connect(own_addr_type, &s_devices[index].addr, BLE_CONNECT_TIMEOUT_MS,
                          NULL, ble_scan_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error initiating connect; rc=%d", rc);
        s_connect_result = BLE_CONNECT_FAILED;
    }
}

ble_connect_result_t ble_scan_get_connect_result(void) {
    return s_connect_result;
}

bool ble_scan_get_rssi(int *rssi) {
    if (!s_connected) return false;

    int8_t r;
    if (ble_gap_conn_rssi(s_conn_handle, &r) != 0) return false;

    if (rssi) *rssi = r;
    return true;
}

void ble_scan_disconnect(void) {
    if (!s_connected) return;
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    s_connected = false;
}
