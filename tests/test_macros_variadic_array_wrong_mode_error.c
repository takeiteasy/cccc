EXPECT_COMPILE_ERROR

[[cccc::comptime]]
void bad(...) {
    VarargAsArray();
}

bad(name);

int main(void) {
    return 42;
}
