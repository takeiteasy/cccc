// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a template must be a definition
@template(T) T (*fp)(T);

int main(void) {
    return 42;
}
