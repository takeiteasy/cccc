// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not valid in preprocessor expressions

#if __FILE__
int x;
#endif

int main(void) {
    return 42;
}
