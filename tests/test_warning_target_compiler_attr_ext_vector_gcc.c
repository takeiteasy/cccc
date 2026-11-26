// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_EXPECT_STDERR: error: 'ext_vector_type' is not supported by gcc and is dropped \[-Wtarget-compiler\]
typedef float f4 __attribute__((ext_vector_type(4)));
int main(void) {
    return 42;
}
