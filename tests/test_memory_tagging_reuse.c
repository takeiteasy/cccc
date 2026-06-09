// EXPECT_RUNTIME_ERROR
// CCCC_FLAGS: --memory-tagging -V
// CCCC_EXPECT_STDERR: TEMPORAL SAFETY VIOLATION
// Test temporal memory tagging with explicit memory reuse

void *malloc(long size);
void free(void *ptr);

struct Data {
    int value;
    int count;
};

int main() {
    struct Data *data1 = (struct Data *)malloc(sizeof(struct Data));
    data1->value = 42;
    data1->count = 1;

    struct Data *stale = data1;

    free(data1);

    struct Data *data2 = (struct Data *)malloc(sizeof(struct Data));
    data2->value = 100;
    data2->count = 2;

    // stale was tagged with generation N, memory is now generation N+1
    int bad_value = stale->value;

    return bad_value;
}
