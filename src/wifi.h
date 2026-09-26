#ifndef WIFI_H
#define WIFI_H

void wifi_init(const char *ssid, const char *pass);
int wifi_get_rssi();
bool wifi_is_connected();

#endif