// Tests __attribute__((destructor(priority))) ordering: reverse of
// constructor order — higher priority numbers run first; destructors with
// no explicit priority form the default group and run first of all.
// CCCC_EXPECT_STDOUT: ddefault\nd200\nd101

#include <stdio.h>

__attribute__((destructor(101))) void d101(void) { printf("d101\n"); }
__attribute__((destructor(200))) void d200(void) { printf("d200\n"); }
__attribute__((destructor)) void ddefault(void) { printf("ddefault\n"); }

int main(void) {
    return 42;
}
