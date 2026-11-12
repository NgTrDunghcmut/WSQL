#include <stdio.h>
#include "record.h"

static void format_nullable_int(char *field, size_t field_size, const int *value) {
    if (value == NULL) {
        snprintf(field, field_size, "null");
    } else {
        snprintf(field, field_size, "%d", *value);
    }
}

int record_to_json(char *buf, size_t buf_size, time_t timestamp,
                    const char *node, const char *technology, int rssi,
                    const int *snr, const int *jitter_ms, const int *latency_ms) {
    char snr_field[16], jitter_field[16], latency_field[16];
    format_nullable_int(snr_field, sizeof(snr_field), snr);
    format_nullable_int(jitter_field, sizeof(jitter_field), jitter_ms);
    format_nullable_int(latency_field, sizeof(latency_field), latency_ms);

    int n = snprintf(buf, buf_size,
        "{\"timestamp\":%lld,\"node\":\"%s\",\"technology\":\"%s\","
        "\"rssi\":%d,\"snr\":%s,\"jitter\":%s,\"latency\":%s}",
        (long long)timestamp, node, technology, rssi,
        snr_field, jitter_field, latency_field);

    if (n < 0 || (size_t)n >= buf_size) return -1;
    return n;
}
