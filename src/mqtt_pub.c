#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "mqtt_pub.h"
#include "pins_stats.h"

static const char *TAG = "MQTTPub";
static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Connected to broker");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected from broker");
        break;
    default:
        break;
    }
}

void mqtt_pub_init(void) {
    if (s_client != NULL) return; // already started - safe to call again

    esp_mqtt_client_config_t config = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    s_client = esp_mqtt_client_init(&config);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
}

bool mqtt_pub_is_connected(void) {
    return s_connected;
}

bool mqtt_pub_send(const char *payload) {
    if (s_client == NULL) return false;
    int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC, payload, 0, 0, 0);
    ESP_LOGI(TAG, "Sending to '%s': %s (msg_id=%d)", MQTT_TOPIC, payload, msg_id);
    return msg_id >= 0;
}
