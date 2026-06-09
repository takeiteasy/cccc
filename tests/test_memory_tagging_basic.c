// EXPECT_RUNTIME_ERROR
// JCC_FLAGS: --memory-tagging -V
// JCC_EXPECT_STDERR: TEMPORAL SAFETY VIOLATION
// Test basic temporal memory tagging - UAF with memory reuse
// This should FAIL when run with --memory-tagging flag

void *malloc(long size);
void free(void *ptr);

int main() {
    int *ptr1 = (int *)malloc(sizeof(int) * 10);
    *ptr1 = 42;

    int *stale_ptr = ptr1;

    free(ptr1);

    int *ptr2 = (int *)malloc(sizeof(int) * 10);
    *ptr2 = 100;

    // stale_ptr has generation 0 but memory now has generation 1
    int value = *stale_ptr;

    return value;
}
