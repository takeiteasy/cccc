// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must return an integer or bool
typedef struct {
    int x;
} A;

[[cccc::operator(==)]]
A eq(A a, A b) {
    return a;
}

int main(void) {
    return 0;
}
