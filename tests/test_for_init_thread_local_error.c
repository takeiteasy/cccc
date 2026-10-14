// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: '_Thread_local' variable in 'for' loop initial declaration must be static or extern
int main(void) {
    for (_Thread_local int t = 0; t < 1; t++) {
    }
    return 42;
}
