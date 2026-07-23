// Expected return: 42
// #409: previously-missing sys/wait.h, sys/stat.h, sys/socket.h constants.
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

int main(void) {
    if (WCONTINUED == 0)
        return 1;
    if (UTIME_NOW == UTIME_OMIT)
        return 2;

    // Two separate namespaces (setsockopt SO_* vs recv/send MSG_* flags) --
    // checked for distinctness within each namespace only. Values may
    // legitimately collide across namespaces (e.g. SO_LINGER == MSG_DONTWAIT
    // on Darwin), since they're never compared against each other.
    long so_vals[] = {
        SO_ERROR, SO_KEEPALIVE, SO_BROADCAST, SO_RCVBUF, SO_SNDBUF,
        SO_LINGER, SO_TYPE,
    };
    int so_n = sizeof(so_vals) / sizeof(so_vals[0]);
    for (int i = 0; i < so_n; i++) {
        for (int j = i + 1; j < so_n; j++) {
            if (so_vals[i] == so_vals[j])
                return 3;
        }
    }

    long msg_vals[] = { MSG_PEEK, MSG_DONTWAIT, MSG_WAITALL, MSG_OOB };
    int msg_n = sizeof(msg_vals) / sizeof(msg_vals[0]);
    for (int i = 0; i < msg_n; i++) {
        for (int j = i + 1; j < msg_n; j++) {
            if (msg_vals[i] == msg_vals[j])
                return 4;
        }
    }

    return 42;
}
