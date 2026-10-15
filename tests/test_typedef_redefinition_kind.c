// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'K' redeclared as a different kind of symbol
enum { K = 1 };
typedef int K;

int main(void) {
    return 42;
}
