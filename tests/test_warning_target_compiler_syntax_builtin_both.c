// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_REJECT_STDERR: target-compiler|error
// CCCC_EXPECT_STDOUT: __builtin_convertvector
typedef int   v4 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));
f4 c(v4 a) {
    return __builtin_convertvector(a, f4);
}
int main(void) {
    return 42;
}
