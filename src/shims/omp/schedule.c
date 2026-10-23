void omp_set_schedule(omp_sched_t kind, int chunk) {
    __cccc_omp_sched_set((int)kind, chunk);
}

void omp_get_schedule(omp_sched_t *kind, int *chunk) {
    int k;
    __cccc_omp_sched_get(&k, chunk);
    *kind = (omp_sched_t)k;
}
