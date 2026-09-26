#ifndef PING_H
#define PING_H

#include <stdbool.h>

// Sends one ICMP echo and waits for the reply.
// On success returns true and writes the round-trip time (ms) to *rtt_ms.
// On timeout/failure returns false (packet considered lost).
bool ping_once(const char *ip, int *rtt_ms);

void ping_gateway(const char *ip);

#endif
