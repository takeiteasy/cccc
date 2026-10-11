void omp_init_nest_lock(omp_nest_lock_t *lock) {
    __cccc_omp_lock_init(&lock->__impl);
}

void omp_destroy_nest_lock(omp_nest_lock_t *lock) {
    __cccc_omp_lock_destroy(&lock->__impl);
}

void omp_set_nest_lock(omp_nest_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (__cccc_omp_lock_mine(l)) {
        l->depth++;
        return;
    }
    pthread_mutex_lock(&l->m);
    __cccc_omp_lock_take(l);
}

void omp_unset_nest_lock(omp_nest_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (--l->depth == 0) {
        __cccc_omp_lock_drop(l);
        pthread_mutex_unlock(&l->m);
    }
}

int omp_test_nest_lock(omp_nest_lock_t *lock) {
    struct __cccc_omp_lock *l = __cccc_omp_lock_get(&lock->__impl);
    if (__cccc_omp_lock_mine(l))
        return ++l->depth;
    if (pthread_mutex_trylock(&l->m))
        return 0;
    __cccc_omp_lock_take(l);
    return 1;
}
