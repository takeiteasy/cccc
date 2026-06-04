// EXPECT_COMPILE_ERROR
// JCC_FLAGS: -Werror=shadow
// JCC_EXPECT_STDERR: error: declaration of 'value' shadows an outer variable \[-Wshadow\]

int value;

int main(void) {
    int value = 42;
    return value;
}
