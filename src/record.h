#ifndef RECORD_H
#define RECORD_H

#include <stddef.h>
#include <time.h>

// Formats one sample as a single-line JSON object matching the
// ClickHouse ingestion schema, e.g.:
//   {"timestamp":1758618000,"node":"KingSlayer","technology":"Wi-Fi",
//    "rssi":-63,"snr":null,"jitter":3,"latency":24}
// snr/jitter_ms/latency_ms are written as JSON null when passed as NULL -
// pass NULL for whichever fields don't apply to this technology (LoRa:
// snr only; WiFi: jitter/latency only; BLE: neither).
// Returns the number of bytes written (excluding the null terminator),
// or -1 if buf_size was too small to hold the result.
int record_to_json(char *buf, size_t buf_size, time_t timestamp,
                    const char *node, const char *technology, int rssi,
                    const int *snr, const int *jitter_ms, const int *latency_ms);

#endif
