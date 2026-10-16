// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Werror=unknown-attributes
// CCCC_EXPECT_STDERR: error: unknown attribute 'no_such_handler' ignored \[-Wunknown-attributes\]
int x @no_such_handler;

int main(void) {
    return 42;
}
