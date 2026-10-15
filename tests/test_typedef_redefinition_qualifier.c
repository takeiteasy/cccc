// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: typedef redefinition with different types: 'T'
typedef const int T;
typedef int       T;

int main(void) {
    return 42;
}
