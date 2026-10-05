// EXPECT_COMPILE_ERROR
// Unscoped [[cold]] is not a standard attribute, so it still warns even though
// __attribute__((cold)) and [[gnu::cold]] are accepted silently.
// CCCC_FLAGS: -Werror=attributes
[[cold]] void f(void);
int main(void) {
    return 42;
}
