// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Werror=shadow
// CCCC_EXPECT_STDERR: error: declaration of 'value' shadows an outer variable \[-Wshadow\]

int value;

int main(void) {
    int value = 42;
    return value;
}
