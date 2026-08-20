// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --memory-tagging
// Test temporal memory tagging across multiple alloc/free/reallocate cycles:
// a pointer tagged with an old generation is a temporal safety violation no
// matter how many generations the underlying block has cycled through since.

void *malloc(long size);
void free(void *ptr);

int main() {
    // Allocate memory (generation 0)
    int *ptr1   = (int *)malloc(sizeof(int) * 10);
    *ptr1       = 10;
    int *stale1 = ptr1; // Tagged with generation 0

    // Free and reallocate (generation 1)
    free(ptr1);
    int *ptr2   = (int *)malloc(sizeof(int) * 10);
    *ptr2       = 20;
    int *stale2 = ptr2; // Tagged with generation 1

    // Free and reallocate again (generation 2)
    free(ptr2);
    int *ptr3   = (int *)malloc(sizeof(int) * 10);
    *ptr3       = 30;
    int *stale3 = ptr3; // Tagged with generation 2

    // Free and reallocate once more (generation 3)
    free(ptr3);
    int *ptr4 = (int *)malloc(sizeof(int) * 10);
    *ptr4     = 40;

    // Try to use first stale pointer (tagged with generation 0, memory is now
    // generation 3) This should trigger temporal safety violation
    int bad_value = *stale1;

    return bad_value; // unreachable: --memory-tagging aborts on the read above
}
