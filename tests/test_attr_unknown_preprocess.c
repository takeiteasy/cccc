// Tests that -E passes through __attribute__((...)) text verbatim.
// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: __attribute__\(\(used\)\)
// CCCC_EXPECT_STDOUT: __attribute__\(\(visibility\("default"\)\)\)

__attribute__((used)) int x = 5;
__attribute__((visibility("default"))) int foo(void) { return 1; }
int main(void) { return 42; }
