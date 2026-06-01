#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    struct hostent *he = gethostbyname("localhost");
    if (!he) return 1;
    if (he->h_addrtype != AF_INET) return 2;
    if (he->h_length != 4) return 3;

    struct addrinfo hints;
    struct addrinfo *res = 0;
    for (int i = 0; i < (int)(sizeof(hints)); i++)
        ((char *)&hints)[i] = 0;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo("127.0.0.1", "80", &hints, &res);
    if (gai != 0) return 4;
    if (!res) return 5;
    if (res->ai_family != AF_INET) return 6;
    if (res->ai_addrlen < (socklen_t)sizeof(struct sockaddr_in)) return 7;
    freeaddrinfo(res);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 8;
    close(fd);

    struct sockaddr_in addr;
    for (int i = 0; i < (int)(sizeof(addr)); i++)
        ((char *)&addr)[i] = 0;
#ifdef __APPLE__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    socklen_t len = sizeof(addr);
    if (bind(-1, (struct sockaddr *)&addr, sizeof(addr)) != -1) return 9;
    if (listen(-1, 1) != -1) return 10;
    if (accept(-1, (struct sockaddr *)&addr, &len) != -1) return 11;
    if (connect(-1, (struct sockaddr *)&addr, sizeof(addr)) != -1) return 12;
    if (getsockname(-1, (struct sockaddr *)&addr, &len) != -1) return 13;

    return 42;
}
