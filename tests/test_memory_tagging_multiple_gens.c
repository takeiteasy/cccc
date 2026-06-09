// EXPECT_RUNTIME_ERROR
// JCC_FLAGS: --memory-tagging -V
// JCC_EXPECT_STDERR: TEMPORAL SAFETY VIOLATION
// Test temporal memory tagging with multiple alloc/free cycles

void *malloc(long size);
void free(void *ptr);

int main() {
    int *ptr1 = (int *)malloc(sizeof(int) * 10);
    *ptr1 = 10;
    int *stale1 = ptr1; // tagged with generation 0

    free(ptr1);
    int *ptr2 = (int *)malloc(sizeof(int) * 10);
    *ptr2 = 20;

    free(ptr2);
    int *ptr3 = (int *)malloc(sizeof(int) * 10);
    *ptr3 = 30;

    free(ptr3);
    int *ptr4 = (int *)malloc(sizeof(int) * 10);
    *ptr4 = 40;

    // stale1 has generation 0, allocation at stale1's address has generation 1
    int bad_value = *stale1;

    return bad_value;
}
