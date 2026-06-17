// Expected return: 42
#include <threads.h>

static mtx_t g_mtx;
static int   g_counter = 0;

static int increment(void *arg) {
    int n = *(int *)arg;
    for (int i = 0; i < n; i++) {
        mtx_lock(&g_mtx);
        g_counter++;
        mtx_unlock(&g_mtx);
    }
    return 0;
}

int main(void) {
    if (mtx_init(&g_mtx, mtx_plain) != thrd_success)
        return 1;

    thrd_t t1, t2;
    int n = 50;
    if (thrd_create(&t1, increment, &n) != thrd_success) return 2;
    if (thrd_create(&t2, increment, &n) != thrd_success) return 3;

    thrd_join(t1, 0);
    thrd_join(t2, 0);
    mtx_destroy(&g_mtx);

    return g_counter == 100 ? 42 : 4;
}
