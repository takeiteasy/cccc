// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: floating constant in preprocessor expression

#if 0.5
int x;
#endif

int main(void) {
    return 42;
}
