#include <stdlib.h>
#include <stdio.h>

// #699: reallocarray(ptr, nmemb, size) -- previously declared in
// include/stdlib.h with an alloc_size attribute (for #649) but not
// registered in the VM's FFI table or codegen allocator dispatch, so any
// call to it failed with "undefined function: reallocarray". Now routes
// through the overflow-checked REALCA opcode on the default VM-heap path,
// and through the cccc_reallocarray polyfill on the --no-vm-heap (-V) host
// allocator path.

int main(void) {
    // Basic grow: data is preserved, like realloc.
    int *arr = malloc(3 * sizeof(int));
    if (!arr) {
        printf("FAIL: malloc returned NULL\n");
        return 1;
    }
    arr[0]    = 10;
    arr[1]    = 20;
    arr[2]    = 30;

    int *arr2 = reallocarray(arr, 5, sizeof(int));
    if (!arr2) {
        printf("FAIL: reallocarray returned NULL\n");
        return 1;
    }
    if (arr2[0] != 10 || arr2[1] != 20 || arr2[2] != 30) {
        printf("FAIL: reallocarray lost data\n");
        return 1;
    }
    printf("PASS: reallocarray grows and preserves data\n");

    // reallocarray(NULL, nmemb, size) behaves like calloc-sized realloc(NULL,
    // ...).
    int *fresh = reallocarray(NULL, 4, sizeof(int));
    if (!fresh) {
        printf("FAIL: reallocarray(NULL, ...) returned NULL\n");
        return 1;
    }
    fresh[3] = 99;
    if (fresh[3] != 99) {
        printf("FAIL: reallocarray(NULL, ...) result unusable\n");
        return 1;
    }
    printf("PASS: reallocarray(NULL, ...) works like malloc\n");

    // Overflow guard: nmemb * size would overflow -- must return NULL and
    // leave the original allocation intact (unlike realloc(ptr, 0), which
    // frees). This is the entire reason to use reallocarray over
    // realloc(ptr, nmemb * size).
    void *overflow_result =
        reallocarray(arr2, (size_t)1 << 60, (size_t)1 << 60);
    if (overflow_result != NULL) {
        printf("FAIL: reallocarray did not detect overflow\n");
        return 1;
    }
    // arr2 must still be valid (not freed by the failed call).
    if (arr2[0] != 10 || arr2[1] != 20 || arr2[2] != 30) {
        printf("FAIL: reallocarray overflow path corrupted/freed the original "
               "block\n");
        return 1;
    }
    printf("PASS: reallocarray overflow guard leaves original allocation "
           "intact\n");

    free(arr2);
    free(fresh);

    printf("All tests passed!\n");
    return 42;
}
