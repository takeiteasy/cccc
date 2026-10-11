struct __cccc_omp_lock {
    pthread_mutex_t m;
    unsigned long   owner; // thread holding it, 0 when free
    int             depth;
};

static struct __cccc_omp_lock *__cccc_omp_lock_get(void **slot) {
    return (struct __cccc_omp_lock *)*slot;
}

static void __cccc_omp_lock_init(void **slot) {
    struct __cccc_omp_lock *l = malloc(sizeof(*l));
    pthread_mutex_init(&l->m, 0);
    l->owner = 0;
    l->depth = 0;
    *slot    = l;
}

static void __cccc_omp_lock_destroy(void **slot) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(slot);
    if (l) {
        pthread_mutex_destroy(&l->m);
        free(l);
    }
    *slot = 0;
}

static int __cccc_omp_lock_mine(struct __cccc_omp_lock *l) {
    return __atomic_load_n(&l->owner, __ATOMIC_ACQUIRE) ==
           (unsigned long)pthread_self();
}

static void __cccc_omp_lock_take(struct __cccc_omp_lock *l) {
    l->depth = 1;
    __atomic_store_n(&l->owner, (unsigned long)pthread_self(), __ATOMIC_RELEASE);
}

static void __cccc_omp_lock_drop(struct __cccc_omp_lock *l) {
    __atomic_store_n(&l->owner, 0, __ATOMIC_RELEASE);
}
