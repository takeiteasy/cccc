// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: call to undeclared function 'make'
// CCCC_EXPECT_STDERR: ^1 error generated
// CCCC_REJECT_STDERR: 2 errors generated
//
// An error in a struct or union initializer expression is reported once, not
// again when the initializer is re-read.

typedef struct {
    int x;
} P;

int main(void) {
    P p = make(1);
    return p.x;
}
