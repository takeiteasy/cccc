int omp_get_num_threads(void) { return __cccc_omp_num_threads(); }
int omp_get_thread_num(void) { return __cccc_omp_thread_num(); }
int omp_get_max_threads(void) { return __cccc_pool_default_threads(); }
int omp_get_thread_limit(void) { return __cccc_pool_default_threads(); }
int omp_in_parallel(void) { return __cccc_omp_num_threads() > 1; }

int omp_get_num_procs(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

void omp_set_num_threads(int n) {
    __cccc_pool_requested = n;
}
void omp_set_dynamic(int on) { (void)on; }
int  omp_get_dynamic(void) { return 0; }

double omp_get_wtime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

double omp_get_wtick(void) { return 1e-9; }
