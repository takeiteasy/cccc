// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must also have the 'overloadable' attribute

int main(void) {
    int __attribute__((overloadable)) q(int);
    return 0;
}

int q(int x) {
    return x;
}
