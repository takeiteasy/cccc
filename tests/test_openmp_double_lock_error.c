// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -fopenmp
#include <omp.h>
int main(void) {
    omp_lock_t lock;
    omp_init_lock(&lock);
    omp_set_lock(&lock);
    omp_set_lock(&lock);
    return 42;
}
