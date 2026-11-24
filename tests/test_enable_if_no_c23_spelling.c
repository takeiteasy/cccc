// CCCC_EXPECT_STDERR: 'enable_if' has no \[\[ \]\] spelling; write __attribute__\(\(enable_if\)\)
//
// enable_if and diagnose_if have only the GNU spelling, as in clang.

int f(int n) [[clang::enable_if(n > 0, "")]];

int main(void) {
    return 42;
}
