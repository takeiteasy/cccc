// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot be both static and an instance method

struct P {
    int x;
};
[[cccc::method(struct P, f)]]
__attribute__((overloadable))
int f(struct P *p, int k) {
    return 1;
}
[[cccc::static_method(struct P, f)]]
__attribute__((overloadable))
int f(double k) {
    return 1;
}

int main(void) {
    return 0;
}
