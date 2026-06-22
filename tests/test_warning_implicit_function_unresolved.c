// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Wimplicit-function-declaration
// CCCC_EXPECT_STDERR: undefined function: missing
// CCCC_C4_SKIP: compile_only (-c) emits text relocations for unresolved symbols
int main(void) {
    return missing();
}
