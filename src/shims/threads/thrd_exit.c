_Noreturn void thrd_exit(int res) {
    pthread_exit((void *)(long)res);
}
