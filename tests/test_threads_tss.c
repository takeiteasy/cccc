// Expected return: 42
// Known --leaks flag (not suppressed): worker's tss_set'd allocation is
// never freed by dtor because tss_create's destructor is not invoked on
// thread exit (C11 7.26.6.1p2 nonconformance) -- tracked separately.
#include <threads.h>
#include <stdlib.h>

static tss_t g_key;

static void dtor(void *p) {
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

    tss_delete(g_key);
    return res == 0 ? 42 : 4;
}
