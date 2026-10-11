void omp_init_lock(omp_lock_t *lock) { __cccc_omp_lock_init(&lock->__impl); }

void omp_destroy_lock(omp_lock_t *lock) {
    __cccc_omp_lock_destroy(&lock->__impl);
}

void omp_set_lock(omp_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (__cccc_omp_lock_mine(l)) {
        write(2, "omp_set_lock: lock is already held (deadlock)\n", 46);
        __builtin_trap();
    }
    pthread_mutex_lock(&l->m);
    __cccc_omp_lock_take(l);
}

void omp_unset_lock(omp_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    __cccc_omp_lock_drop(l);
    pthread_mutex_unlock(&l->m);
}

int omp_test_lock(omp_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (pthread_mutex_trylock(&l->m))
        return 0;
    __cccc_omp_lock_take(l);
    return 1;
}
