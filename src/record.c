#include <stdio.h>
#include "record.h"

int record_to_json(char *buf, size_t buf_size, time_t timestamp, int rssi, int rtt_ms, bool lost) {
    int n;
    if (lost) {
        n = snprintf(buf, buf_size,
            "{\"timestamp\":%lld,\"rssi\":%d,\"rtt_ms\":null,\"lost\":true}",
            (long long)timestamp, rssi);
    } else {
        n = snprintf(buf, buf_size,
            "{\"timestamp\":%lld,\"rssi\":%d,\"rtt_ms\":%d,\"lost\":false}",
            (long long)timestamp, rssi, rtt_ms);
    }

    if (n < 0 || (size_t)n >= buf_size) return -1;
    return n;
}
