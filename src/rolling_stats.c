#include "rolling_stats.h"

void rolling_stats_init(rolling_stats_t *s) {
    s->count = 0;
    s->head = 0;
}

void rolling_stats_push(rolling_stats_t *s, int value) {
    s->values[s->head] = value;
    s->head = (s->head + 1) % ROLLING_STATS_CAP;
    if (s->count < ROLLING_STATS_CAP) s->count++;
}

bool rolling_stats_get(const rolling_stats_t *s, int *avg, int *max, int *min) {
    if (s->count == 0) return false;

    long sum = 0;
    int mx = s->values[0];
    int mn = s->values[0];
    for (int i = 0; i < s->count; i++) {
        int v = s->values[i]; // order doesn't matter for avg/max/min
        sum += v;
        if (v > mx) mx = v;
        if (v < mn) mn = v;
    }

    if (avg) *avg = (int)(sum / s->count);
    if (max) *max = mx;
    if (min) *min = mn;
    return true;
}
