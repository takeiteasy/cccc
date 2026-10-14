// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unsupported type
// __sync_* operates on 1/2/4/8-byte integers and pointers only.
int main(void) {
    double d = 1.0;
    __sync_fetch_and_add(&d, 1.0);
    return 42;
}
