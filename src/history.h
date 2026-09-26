#ifndef HISTORY_H
#define HISTORY_H

// Number of most-recent RSSI samples the chart can show.
#define RSSI_HISTORY_CAP 40

void rssi_history_push(int rssi);

// Copies up to max_out most-recent samples into out, oldest first.
// Returns the number of samples actually copied.
int rssi_history_get(int *out, int max_out);

#endif
