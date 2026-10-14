// A block-scope `static _Thread_local` variable is private to each thread.
#include <threads.h>

static int *slot(void) {
    static _Thread_local int x;
    return &x;
}

static int *slot_auto(void) {
    static thread_local auto y = 3;
    return &y;
}

static int worker(void *arg) {
    (void)arg;
    *slot()      = 5;
    *slot_auto() = 9;
    return 0;
}

int main(void) {
    thrd_t t;
    if (thrd_create(&t, worker, 0) != thrd_success)
        return 1;
    thrd_join(t, 0);
    return *slot() == 0 && *slot_auto() == 3 ? 42 : 2;
}
