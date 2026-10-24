// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operands to \[\]
typedef struct {
    int x;
} A;

int main(void) {
    A a = {1};
    return a[0];
}
