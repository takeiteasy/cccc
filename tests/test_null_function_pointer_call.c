// CCCC_NATIVE_SKIP: VM-only diagnostic (the host faults)
// EXPECT_RUNTIME_ERROR
// CCCC_EXPECT_STDOUT: call through a null function pointer
int main(void) {
    int (*volatile p)(long) = 0;
    return p(0);
}
