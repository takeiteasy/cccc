// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: method 'f' is already defined for this type

struct P {
    int x;
};
[[cccc::method(struct P, f)]]
__attribute__((overloadable))
int first(struct P *p, int k) {
    return 1;
}
[[cccc::method(struct P, f)]]
__attribute__((overloadable))
int second(struct P *p, int j) {
    return 2;
}

int main(void) {
    return 0;
}
