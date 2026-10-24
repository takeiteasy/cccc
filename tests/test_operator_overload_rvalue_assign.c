// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expression is not assignable
typedef struct {
    int x;
} A;

[[cccc::operator(+)]]
A add_a(A *a, A b) {
    return b;
}

A make(void) {
    return (A){1};
}

int main(void) {
    A b = {2};
    make() += b;
    return 0;
}
