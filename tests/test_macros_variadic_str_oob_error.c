// EXPECT_COMPILE_ERROR
// Ticket #284: string vararg access is bounds checked.

[[cccc::comptime]]
void bad_str(...) {
    VarargStrAt(VarargCount());
}

bad_str(foo);

int main(void) {
    return 42;
}
