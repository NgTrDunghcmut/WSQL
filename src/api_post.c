#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "api_post.h"
#include "pins.h"

static const char *TAG = "APIPost";

bool api_post_send(const char *body) {
    esp_http_client_config_t config = {
        .url = API_POST_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    bool ok = false;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ok = (status >= 200 && status < 300);
        ESP_LOGI(TAG, "POST %s -> status=%d", API_POST_URL, status);
    } else {
        ESP_LOGE(TAG, "POST %s failed: %s", API_POST_URL, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return ok;
}
