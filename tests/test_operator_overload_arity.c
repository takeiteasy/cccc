// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must take two parameters
typedef struct {
    int x;
} A;

[[cccc::operator(*)]]
A mul(A a) {
    return a;
}

int main(void) {
    return 0;
}
