// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot overload
typedef struct {
    int x;
} A;

[[cccc::operator(&&)]]
A f(A a, A b) {
    return a;
}

int main(void) {
    return 0;
}
