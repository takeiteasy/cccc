// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: pthread_t \*volatile b;
// CCCC_EXPECT_STDOUT: const pthread_t \*c;
// CCCC_REJECT_STDOUT: void \*\*(volatile )?[abc];
// A pointer to a host pointer typedef keeps the typedef name in its declaration.
#include <pthread.h>

int main(void) {
    pthread_t              t;
    pthread_t             *a = &t;
    pthread_t *volatile    b = &t;
    const pthread_t       *c = &t;
    return (a == b && c == b) ? 42 : 1;
}
