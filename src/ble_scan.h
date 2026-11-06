#ifndef BLE_SCAN_H
#define BLE_SCAN_H

#include <stdbool.h>
#include <stddef.h>

// Maximum distinct nearby (named) devices tracked at once (also the
// max rows shown on the scan-list screen).
#define BLE_SCAN_MAX_DEVICES 5

// Longest device name stored/displayed, including the null terminator.
#define BLE_SCAN_NAME_LEN 32

typedef enum {
    BLE_CONNECT_NONE = 0, // no connect attempt made yet
    BLE_CONNECT_PENDING,
    BLE_CONNECT_SUCCESS,
    BLE_CONNECT_FAILED,
} ble_connect_result_t;

// Initializes NimBLE (if not already running) and starts a continuous
// active scan, collecting up to BLE_SCAN_MAX_DEVICES distinct nearby
// devices that advertise a name - unnamed devices are ignored entirely.
// Safe to call again after a previous BLE-mode session.
void ble_scan_init(void);

// Number of distinct named devices seen so far (capped at
// BLE_SCAN_MAX_DEVICES).
int ble_scan_get_device_count(void);

// Writes device `index`'s name to buf. Returns false if out of range.
bool ble_scan_get_device_name(int index, char *buf, size_t buf_size);

// Cancels discovery and initiates a connection to device `index`.
// Poll ble_scan_get_connect_result() to see the outcome.
void ble_scan_connect_device(int index);

// Current state of the most recent connect attempt.
ble_connect_result_t ble_scan_get_connect_result(void);

// True if currently connected; writes the connection's live RSSI (dBm,
// updated whenever a data packet is received on the link) to *rssi.
bool ble_scan_get_rssi(int *rssi);

// Cleanly terminates the active connection, if any. Call before leaving
// BLE mode so a later session starts from a clean state.
void ble_scan_disconnect(void);

#endif
