// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'twice' is not a template in comptime code
// A runtime template is not visible to comptime code.
@template(T) T twice(T v) {
    return v * 2;
}

@comptime int ct(void) {
    return @inst(int) twice(21);
}

int main(void) {
    return 0;
}
