#ifndef API_POST_H
#define API_POST_H

#include <stdbool.h>

// Sends an HTTP POST of `body` to API_POST_URL (pins.h), with
// Content-Type: application/json. WiFi must already be connected.
// Returns true if the server responded with a 2xx status.
bool api_post_send(const char *body);

#endif
