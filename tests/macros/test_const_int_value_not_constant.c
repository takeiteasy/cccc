// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @weight expects an integer constant

@comptime(attribute("weight")) void define_weight(AttrTarget *target, ...) {
    int64_t w = 0;
    if (VarargCount() != 1 || !ConstIntValue(VarargAt(0), &w))
        MacroErrorAt(MakeIntLiteral(0), "@weight expects an integer constant");
}

int x;
@weight(x) int g;

int main(void) {
    return 42;
}
