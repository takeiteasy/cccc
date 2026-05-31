#include <arpa/inet.h>

int main(void) {
    if (htonl(0x12345678) == 0x12345678) {
        /* little-endian host */
        if (htonl(0x12345678) != 0x78563412) return 1;
        if (htons(0x1234) != 0x3412) return 2;
        if (ntohl(0x78563412) != 0x12345678) return 3;
        if (ntohs(0x3412) != 0x1234) return 4;
    }

    if (inet_addr("127.0.0.1") == (uint32_t)(-1)) return 5;

    struct in_addr addr;
    if (inet_pton(AF_INET, "127.0.0.1", &addr) != 1) return 6;
    if (addr.s_addr != inet_addr("127.0.0.1")) return 7;

    char buf[32];
    const char *s = inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    if (!s) return 8;
    if (s[0] != '1' || s[1] != '2' || s[2] != '7') return 9;

    return 42;
}
