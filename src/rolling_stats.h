#ifndef ROLLING_STATS_H
#define ROLLING_STATS_H

#include <stdbool.h>
#include "pins_stats.h" // ROLLING_STATS_CAP

// Fixed-capacity ring buffer of the most recent samples, used to turn
// an unbounded "lifetime average/max/min" into one over just the last
// ROLLING_STATS_CAP samples (see pins_stats.h to change the window size).

typedef struct {
    int values[ROLLING_STATS_CAP];
    int count; // samples held so far, caps at ROLLING_STATS_CAP
    int head;  // next write index
} rolling_stats_t;

void rolling_stats_init(rolling_stats_t *s);

// Overwrites the oldest sample once the buffer is full.
void rolling_stats_push(rolling_stats_t *s, int value);

// Computes avg/max/min over the currently-held samples (the last
// min(count, ROLLING_STATS_CAP) pushed - avg divides by however many
// samples actually exist, not always by ROLLING_STATS_CAP). Returns
// false (leaving *avg/*max/*min untouched) if nothing's been pushed.
bool rolling_stats_get(const rolling_stats_t *s, int *avg, int *max, int *min);

#endif
