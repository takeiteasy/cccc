// EXPECT_COMPILE_ERROR
// Ticket #284: AST vararg access is bounds checked.

[[cccc::comptime]]
Node *bad_at(...) {
    return VarargAt(VarargCount());
}

int main(void) {
    return bad_at(1);
}
