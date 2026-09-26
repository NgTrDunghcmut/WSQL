#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "esp_netif_sntp.h"
#include "esp_err.h"
#include "timesync.h"

bool timesync_init(int timeout_ms) {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);

    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        printf("Time sync failed: %s\n", esp_err_to_name(err));
        return false;
    }

    printf("Time synchronized: %lld\n", (long long)time(NULL));
    return true;
}
