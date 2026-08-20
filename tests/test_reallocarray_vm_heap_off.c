// CCCC_FLAGS: -V
#include <stdlib.h>
#include <stdio.h>

// #699: same reallocarray coverage as test_reallocarray.c, but forced onto
// the -V (host allocator) path, which routes through the generic FFI table
// via the cccc_reallocarray polyfill in src/stdlib/stdlib.c rather than the
// VM-heap REALCA opcode. Pins that the overflow check isn't accidentally
// only implemented on one of the two dispatch paths.

int main(void) {
    int *arr = malloc(3 * sizeof(int));
    if (!arr) {
        printf("FAIL: malloc returned NULL\n");
        return 1;
    }
    arr[0]    = 1;
    arr[1]    = 2;
    arr[2]    = 3;

    int *arr2 = reallocarray(arr, 6, sizeof(int));
    if (!arr2) {
        printf("FAIL: reallocarray returned NULL\n");
        return 1;
    }
    if (arr2[0] != 1 || arr2[1] != 2 || arr2[2] != 3) {
        printf("FAIL: reallocarray lost data\n");
        return 1;
    }

    void *overflow_result =
        reallocarray(arr2, (size_t)1 << 60, (size_t)1 << 60);
    if (overflow_result != NULL) {
        printf("FAIL: reallocarray did not detect overflow on host-allocator "
               "path\n");
        return 1;
    }
    if (arr2[0] != 1 || arr2[1] != 2 || arr2[2] != 3) {
        printf("FAIL: overflow path corrupted the original block\n");
        return 1;
    }

    free(arr2);
    printf("All tests passed!\n");
    return 42;
}
