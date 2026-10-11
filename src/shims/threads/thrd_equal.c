int thrd_equal(thrd_t a, thrd_t b) {
    pthread_t pa, pb;
    __builtin_memcpy(&pa, &a, sizeof(pa));
    __builtin_memcpy(&pb, &b, sizeof(pb));
    return pthread_equal(pa, pb) != 0;
}
