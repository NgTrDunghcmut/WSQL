#ifndef BLE_SCAN_H
#define BLE_SCAN_H

#include <stdbool.h>
#include <stddef.h>

// Maximum distinct nearby devices tracked at once (also the max rows
// shown on the scan-list screen).
#define BLE_SCAN_MAX_DEVICES 5

// Initializes NimBLE and starts a continuous active scan, collecting
// up to BLE_SCAN_MAX_DEVICES distinct nearby devices by address. Call
// once at boot.
void ble_scan_init(void);

// Number of distinct devices seen so far (capped at BLE_SCAN_MAX_DEVICES).
int ble_scan_get_device_count(void);

// Writes just the device's name (or "(no name)") to buf.
// Returns false if index is out of range.
bool ble_scan_get_device_name(int index, char *buf, size_t buf_size);

// Writes a display label ("NAME RSSI:-70") for device `index` into buf,
// suitable for one row of the scan-list screen.
// Returns false if index is out of range.
bool ble_scan_get_device_label(int index, char *buf, size_t buf_size);

// Locks tracking onto device `index` from the current list. After this,
// ble_scan_get_rssi() reports updates for that specific device only
// (matched by its BLE address, not its name).
void ble_scan_select_device(int index);

// True if the selected device was seen within the last few seconds;
// writes its most recent RSSI (dBm) to *rssi. False if nothing has
// been selected yet via ble_scan_select_device().
bool ble_scan_get_rssi(int *rssi);

#endif
