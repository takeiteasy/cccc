EXPECT_COMPILE_ERROR

[[cccc::comptime]]
void bad(...) {
    _AST_VARARGS_AS_ARRAY();
}

bad(name);

int main(void) {
    return 42;
}
