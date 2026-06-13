EXPECT_COMPILE_ERROR

[[cccc::comptime]]
void bad(...) {
    $vararg_as_array();
}

bad(name);

int main(void) {
    return 42;
}
