// Expected return: 42
// #865: malloc/free/calloc/realloc taken as function-pointer VALUES (rather
// than called directly by name) used to bypass codegen's VM-heap opcode
// routing (is_extern_func_name's special-casing in codegen.c only matches
// the literal `free(...)` call syntax) and fall through to the raw host
// libc function -- fatal for free()/realloc() since a VM-heap pointer's
// preceding bytes are cccc's own AllocHeader, not a real libmalloc chunk.
// Exercises the exact motivating idiom (tss_create(&key, free) /
// pthread_key_create(&key, free), an idiomatic and fully valid C11 pattern)
// plus every direct/indirect combination across the malloc family, with the
// VM heap enabled (the default -- this test intentionally does NOT pass -V).
#include <stdlib.h>
#include <pthread.h>

static pthread_key_t g_key;

static void *worker(void *arg) {
    int *slot = malloc(sizeof(int));
    if (!slot)
        return (void *)1;
    *slot = *(int *)arg;
    pthread_setspecific(g_key, slot);
    return NULL;
}

int main(void) {
    void *(*mp)(size_t)         = malloc;
    void (*fp)(void *)          = free;
    void *(*cp)(size_t, size_t) = calloc;
    void *(*rp)(void *, size_t) = realloc;

    // indirect alloc, direct free
    int *a = mp(sizeof(int));
    if (!a)
        return 1;
    *a = 1;
    free(a);

    // direct alloc, indirect free -- the crash reported in #865
    int *b = malloc(sizeof(int));
    if (!b)
        return 2;
    *b = 2;
    fp(b);

    // both indirect
    int *c = mp(sizeof(int));
    if (!c)
        return 3;
    *c = 3;
    fp(c);

    // indirect calloc must still zero-fill
    int *d = cp(4, sizeof(int));
    if (!d)
        return 4;
    for (int i = 0; i < 4; i++)
        if (d[i] != 0)
            return 5;
    fp(d);

    // indirect realloc growing a directly-malloc'd block preserves data
    int *e = malloc(sizeof(int) * 2);
    if (!e)
        return 6;
    e[0] = 7;
    e[1] = 8;
    e    = rp(e, sizeof(int) * 4);
    if (!e)
        return 7;
    if (e[0] != 7 || e[1] != 8)
        return 8;
    fp(e);

    // The idiom that motivated #865: passing free itself (not a wrapper) as
    // a TSS/pthread-key destructor -- tss_create(&key, free) /
    // pthread_key_create(&key, free), fully valid, idiomatic C11. The
    // destructor fires on the worker thread's exit via #861's
    // run_tss_destructors -> cccc_call_guest_callback path, landing on
    // exactly the indirect-call route this test exercises above.
    if (pthread_key_create(&g_key, free) != 0)
        return 9;
    pthread_t t;
    int       val = 55;
    if (pthread_create(&t, NULL, worker, &val) != 0)
        return 10;
    if (pthread_join(t, NULL) != 0)
        return 11;
    pthread_key_delete(g_key);

    return 42;
}
