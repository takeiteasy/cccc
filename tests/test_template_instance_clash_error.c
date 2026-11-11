// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: tests/fixtures/template_plain_max_tu_b.c
// CCCC_EXPECT_STDERR: redefinition of 'max__int'
@template(T) T max(T a, T b) { return a > b ? a : b; }

int main(void) {
    return @inst(int) max(42, 1);
}
