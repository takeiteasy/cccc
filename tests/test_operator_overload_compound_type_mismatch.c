// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: different type than the left
typedef struct {
    int x;
} A;
typedef struct {
    int x;
} B;

[[cccc::operator(+)]]
B add_ab(A a, A b) {
    return (B){a.x + b.x};
}

int main(void) {
    A a  = {1};
    a   += a;
    return 0;
}
