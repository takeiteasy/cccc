// Regression test for #812: proves the tools/stdlib.tsv wiring for
// sys/shm.h works when that header is included on its own, without any of
// the other sys/ipc.h siblings (sys/sem.h, sys/msg.h) or sys/socket.h/
// sys/un.h pulled in first -- the #792 bug class (a header declaring
// functions but registering none because tools/stdlib.tsv was never
// updated for it).
#include <sys/shm.h>

int main(void) {
    int id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    if (id < 0)
        return 1;

    int *p = (int *)shmat(id, 0, 0);
    if (p == (void *)-1) {
        shmctl(id, IPC_RMID, 0);
        return 2;
    }
    *p      = 7;
    int val = *p;
    shmdt(p);

    shmctl(id, IPC_RMID, 0);
    return val == 7 ? 42 : 3;
}
