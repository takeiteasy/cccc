// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --no-implicit-derive
// CCCC_EXPECT_STDERR: #include <cccc/derive.h> for the built-in attributes

@equals typedef struct {
    int a;
} P;

int main(void) {
    return 42;
}
