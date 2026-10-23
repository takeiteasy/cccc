// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operands to \+
typedef struct {
    int x;
} A;
typedef struct {
    int x;
} B;

[[cccc::operator(+)]]
A add_a(A a, A b) {
    return a;
}

int main(void) {
    A a = {1};
    B b = {2};
    A c = a + b;
    return c.x;
}
