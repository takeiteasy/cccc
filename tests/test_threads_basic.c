// Expected return: 42
#include <threads.h>

static int g_value = 0;

static int worker(void *arg) {
    g_value = *(int *)arg;
    return 0;
}

int main(void) {
    thrd_t t;
    int    arg = 42;
    int    res = -1;

    if (thrd_create(&t, worker, &arg) != thrd_success)
        return 1;
    if (thrd_join(t, &res) != thrd_success)
        return 2;
    if (res != 0)
        return 3;
    return g_value;
}
