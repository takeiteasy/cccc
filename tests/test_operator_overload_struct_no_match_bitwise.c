// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operands to &
typedef struct {
    int x;
} A;

int main(void) {
    A a = {1}, b = {2};
    A c = a & b;
    return c.x;
}
