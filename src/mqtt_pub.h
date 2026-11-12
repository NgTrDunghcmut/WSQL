#ifndef MQTT_PUB_H
#define MQTT_PUB_H

#include <stdbool.h>

// Connects to the configured MQTT broker (pins_stats.h) if not already
// started. Call once after WiFi is connected; safe to call again on a
// later mode entry (no-op if already initialized).
void mqtt_pub_init(void);

// True once the client has connected to the broker.
bool mqtt_pub_is_connected(void);

// Publishes `payload` (QoS 0) to MQTT_TOPIC (pins_stats.h). Returns
// false if not yet connected or the publish failed to send - QoS 0
// messages are never queued, so a failed publish just drops the
// sample rather than retrying.
bool mqtt_pub_send(const char *payload);

#endif
