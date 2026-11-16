// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot be a method or an operator

struct P {
    int x;
};
[[cccc::method(struct P)]]
__attribute__((overloadable))
int len(struct P *p) {
    return 1;
}

int main(void) {
    return 0;
}
