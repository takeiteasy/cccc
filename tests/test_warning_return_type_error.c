// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Werror=return-type
// CCCC_EXPECT_STDERR: error: non-void function should return a value \[-Wreturn-type\]
int value(void) {
    return;
}

int main(void) {
    return value();
}
