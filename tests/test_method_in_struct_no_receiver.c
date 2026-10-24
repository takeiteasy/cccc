// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: method 'f' must take the receiver type as its first parameter
struct S {
    int x;
    int f(void) {
        return 1;
    }
};
int main(void) {
    return 0;
}
