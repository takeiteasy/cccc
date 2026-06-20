// Ticket #292: an explicit [[cccc::comptime(inline)]] annotation inside a
// comptime begin...end block preserves inline semantics (attribute path
// takes precedence over the block extraction path).

#pragma cccc comptime begin

int helper(int n) {
    return n + 1;
}

[[cccc::comptime(inline)]]
Node *make_answer(void) {
    return MakeIntLiteral(helper(41));
}

#pragma cccc comptime end

int main(void) {
    if (make_answer() != 42)
        return 1;
    return 42;
}
