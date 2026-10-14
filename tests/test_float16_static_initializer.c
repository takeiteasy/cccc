// EXPECT_COMPILE_ERROR
// A non-zero _Float16 static initializer is rejected.
static _Float16 g = 1;

int main(void) {
    return 42;
}
