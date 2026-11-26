// CCCC_FLAGS: -E --output-compiler=clang
// CCCC_REJECT_STDERR: (warning|error):
// CCCC_EXPECT_STDOUT: ext_vector_type\(4\)
typedef float f4 __attribute__((ext_vector_type(4)));
int main(void) {
    return 42;
}
