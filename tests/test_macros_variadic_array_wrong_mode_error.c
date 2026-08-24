// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: VarargAsArray is only valid for inline AST macros

[[cccc::comptime]]
void bad(...) {
    VarargAsArray();
}

bad(name);

int main(void) {
    return 42;
}
