#ifndef RECORD_H
#define RECORD_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

// Formats one sample as a single-line JSON object into buf, e.g.:
//   {"timestamp":1758618000,"rssi":-63,"rtt_ms":24,"lost":false}
// When lost is true, rtt_ms is written as null.
// Returns the number of bytes written (excluding the null terminator),
// or -1 if buf_size was too small to hold the result.
int record_to_json(char *buf, size_t buf_size, time_t timestamp, int rssi, int rtt_ms, bool lost);

#endif
