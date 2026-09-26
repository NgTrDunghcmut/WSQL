#include <stdio.h>
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/icmp.h"
#include "lwip/ip4.h"
#include "lwip/inet_chksum.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "ping.h"

static uint16_t s_seq = 0;

bool ping_once(const char *ip, int *rtt_ms) {
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        printf("Error: Cannot create socket\n");
        return false;
    }

    struct timeval timeout = { .tv_sec = 0, .tv_usec = 800000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);

    struct icmp_echo_hdr echo = {0};
    echo.type = ICMP_ECHO;
    echo.code = 0;
    echo.id = 0xAFAF;
    echo.seqno = ++s_seq;
    echo.chksum = 0;
    echo.chksum = inet_chksum(&echo, sizeof(echo));

    int64_t start_us = esp_timer_get_time();
    if (sendto(sock, &echo, sizeof(echo), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }

    char buf[64];
    while (1) {
        int len = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
        if (len < 0) {
            // SO_RCVTIMEO expired, or socket error -> treat as lost
            close(sock);
            return false;
        }

        int ip_hlen = IPH_HL((struct ip_hdr *)buf) * 4;
        if (len < ip_hlen + (int)sizeof(struct icmp_echo_hdr)) continue;

        struct icmp_echo_hdr *reply = (struct icmp_echo_hdr *)(buf + ip_hlen);
        if (reply->type == ICMP_ER && reply->id == echo.id && reply->seqno == echo.seqno) {
            close(sock);
            if (rtt_ms) *rtt_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            return true;
        }
        // Not our reply (stray ICMP traffic) - keep waiting until timeout.
    }
}

void ping_gateway(const char *ip) {
    printf("=== Ping Test to %s ===\n", ip);

    int lost = 0;
    for (int i = 0; i < 4; i++) {
        int rtt_ms = 0;
        if (ping_once(ip, &rtt_ms)) {
            printf("Ping %d: reply in %d ms\n", i + 1, rtt_ms);
        } else {
            printf("Ping %d: lost (timeout)\n", i + 1);
            lost++;
        }
    }

    printf("Ping complete: %d/4 lost\n\n", lost);
}
