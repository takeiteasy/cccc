// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: typedef redefinition with different types: 'T'
typedef struct {
    int a;
} T;
typedef char *T;

int main(void) {
    return 42;
}
