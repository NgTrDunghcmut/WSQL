#ifndef TIMESYNC_H
#define TIMESYNC_H

#include <stdbool.h>

// Starts SNTP and blocks until the system clock is synchronized
// (or timeout_ms elapses). Returns true once time(NULL) gives a real
// Unix timestamp; false if sync didn't complete in time.
bool timesync_init(int timeout_ms);

#endif
