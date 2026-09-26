#include "history.h"

static int s_buf[RSSI_HISTORY_CAP];
static int s_count = 0; // number of valid samples (caps at RSSI_HISTORY_CAP)
static int s_head = 0;  // index the next pushed sample will be written to

void rssi_history_push(int rssi) {
    s_buf[s_head] = rssi;
    s_head = (s_head + 1) % RSSI_HISTORY_CAP;
    if (s_count < RSSI_HISTORY_CAP) s_count++;
}

int rssi_history_get(int *out, int max_out) {
    int n = (s_count < max_out) ? s_count : max_out;
    int start = (s_head - n + RSSI_HISTORY_CAP) % RSSI_HISTORY_CAP;
    for (int i = 0; i < n; i++) {
        out[i] = s_buf[(start + i) % RSSI_HISTORY_CAP];
    }
    return n;
}
