// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: function-scope 't' implicitly auto and declared '_Thread_local'
int main(void) {
    _Thread_local int t = 0;
    return 42;
}
