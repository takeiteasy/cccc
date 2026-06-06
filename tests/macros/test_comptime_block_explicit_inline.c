// Ticket #292: an explicit [[jcc::comptime(inline)]] annotation inside a
// comptime begin...end block preserves inline semantics (attribute path
// takes precedence over the block extraction path).

#pragma jcc comptime begin

int helper(int n) {
    return n + 1;
}

[[jcc::comptime(inline)]]
$node_t *make_answer(void) {
    return $int_literal(helper(41));
}

#pragma jcc end

int main(void) {
    if (make_answer() != 42)
        return 1;
    return 42;
}
