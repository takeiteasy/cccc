// Expected return: 42
#include <threads.h>
#include <stdlib.h>

static tss_t g_key;
static int g_dtor_calls = 0;
static int g_dtor_last_value = -1;

static void dtor(void *p) {
    g_dtor_calls++;
    g_dtor_last_value = *(int *)p;
    free(p);
}

static int worker(void *arg) {
    int *slot = malloc(sizeof(int));
    *slot = *(int *)arg;
    tss_set(g_key, slot);
    int *got = tss_get(g_key);
    return (got && *got == *(int *)arg) ? 0 : 1;
}

int main(void) {
    if (tss_create(&g_key, dtor) != thrd_success)
        return 1;

    thrd_t t;
    int val = 99;
    int res = -1;
    if (thrd_create(&t, worker, &val) != thrd_success) return 2;
    if (thrd_join(t, &res) != thrd_success)            return 3;
    if (res != 0)                                      return 4;

    // C11 7.26.6.1p2: worker's tss_set'd value was non-NULL when it exited,
    // so the destructor must have run exactly once with that value.
    if (g_dtor_calls != 1)      return 5;
    if (g_dtor_last_value != 99) return 6;

    tss_delete(g_key);
    return 42;
}
