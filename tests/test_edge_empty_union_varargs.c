// CCCC_EXPECT_STDOUT: Let's count: 3 0 2 1

#include <stdio.h>

union {} global_empty = {};
union {} global_empty_array[3] = {};

int main(void) {
    union {} local_empty = {};
    union {} var[100] = {};

    (void)global_empty;
    (void)global_empty_array[0];
    (void)local_empty;

    printf("Let's count: %d %d %d %d\n", 3, var[42], 2, 1);
    return 42;
}
