#ifndef BLE_SCAN_H
#define BLE_SCAN_H

#include <stdbool.h>

// Initializes NimBLE and starts a continuous active scan, watching for
// advertisements whose local name matches target_name exactly. Call
// once at boot.
void ble_scan_init(const char *target_name);

// True if the target device's name was seen within the last few
// seconds; writes its most recent RSSI (dBm) to *rssi.
bool ble_scan_get_rssi(int *rssi);

#endif
