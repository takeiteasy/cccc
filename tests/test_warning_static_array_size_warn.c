// CCCC_FLAGS: -Wstatic-array-size
// CCCC_EXPECT_STDERR: array argument has 5 element.*\[-Wstatic-array-size\]
void f(int a[static 10]) {}
int main(void) {
    int buf[5];
    f(buf);
    return 42;
}
