// Tests that __attribute__((destructor)) functions run after main() returns.
// CCCC_EXPECT_STDOUT: main ran\ndtor ran

#include <stdio.h>

__attribute__((destructor)) void fini(void) {
    printf("dtor ran\n");
}

int main(void) {
    printf("main ran\n");
    return 42;
}
