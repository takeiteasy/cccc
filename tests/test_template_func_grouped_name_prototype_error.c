// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a template must be a definition
@template(T) T(fn)(T x);

int main(void) {
    return 42;
}
