// Ticket #292: bare #pragma cccc comptime (no 'begin' keyword) is sugar for
// #pragma cccc comptime begin — the block can still be closed with #pragma cccc end.

#pragma cccc comptime

int triple(int n) {
    return n * 3;
}

int base = 14;

[[cccc::comptime(inline)]]
$node_t *make_answer(void) {
    return $int_literal(triple($get_comptime_int("base")));
}

#pragma cccc end

int main(void) {
    if (make_answer() != 42)
        return 1;
    return 42;
}
