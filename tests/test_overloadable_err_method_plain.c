// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: method 'len' is already defined for this type

struct P {
    int x;
};
[[cccc::method(struct P, len)]]
int plain_len(struct P *p) {
    return 1;
}
[[cccc::method(struct P, len)]]
__attribute__((overloadable))
int len(struct P *p, int k) {
    return 1;
}

int main(void) {
    return 0;
}
