// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: needs a struct or union first operand
typedef struct {
    int x;
} A;

[[cccc::operator([])]]
int *at(int *p, A a) {
    return p + a.x;
}

int main(void) {
    return 0;
}
