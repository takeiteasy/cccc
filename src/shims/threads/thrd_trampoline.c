_Static_assert(sizeof(pthread_t) <= sizeof(void *),
               "cccc: host pthread_t must fit in a pointer-sized thrd_t");
struct __cccc_thrd_args { int (*fn)(void *); void *arg; };
static void *__cccc_thrd_trampoline(void *argp) {
    struct __cccc_thrd_args *a = (struct __cccc_thrd_args *)argp;
    int rc = a->fn(a->arg);
    free(a);
    return (void *)(long)rc;
}
