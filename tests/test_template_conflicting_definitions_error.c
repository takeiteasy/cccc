// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: tests/fixtures/template_other_max_tu_b.c
// CCCC_EXPECT_STDERR: conflicting definitions of template instance 'max__int'
@template(T) T max(T a, T b) { return a > b ? a : b; }

int tu_b_value(void);

int main(void) {
    return @inst(int) max(38, 1) + tu_b_value();
}
