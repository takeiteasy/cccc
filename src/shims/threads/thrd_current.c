thrd_t thrd_current(void) {
    pthread_t self = pthread_self();
    thrd_t out = 0;
    __builtin_memcpy(&out, &self, sizeof(self));
    return out;
}
