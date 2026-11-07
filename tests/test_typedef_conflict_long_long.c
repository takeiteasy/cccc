// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: typedef redefinition with different types

typedef long      T;
typedef long long T;

int main(void) {
    return 42;
}
