#include <stdio.h>
#include "record.h"

int record_to_json(char *buf, size_t buf_size, time_t timestamp, int rssi, int rtt_ms, int jitter_ms, bool lost) {
    char rtt_field[16];
    if (lost) {
        snprintf(rtt_field, sizeof(rtt_field), "null");
    } else {
        snprintf(rtt_field, sizeof(rtt_field), "%d", rtt_ms);
    }

    char jitter_field[16];
    if (jitter_ms < 0) {
        snprintf(jitter_field, sizeof(jitter_field), "null");
    } else {
        snprintf(jitter_field, sizeof(jitter_field), "%d", jitter_ms);
    }

    int n = snprintf(buf, buf_size,
        "{\"timestamp\":%lld,\"rssi\":%d,\"rtt_ms\":%s,\"jitter_ms\":%s,\"lost\":%s}",
        (long long)timestamp, rssi, rtt_field, jitter_field, lost ? "true" : "false");

    if (n < 0 || (size_t)n >= buf_size) return -1;
    return n;
}
