// EXPECT_COMPILE_ERROR JCC_FLAGS: -Werror -Wreturn-type
_Noreturn void test(void) {
    return;
}
int main(void) { return 42; }
