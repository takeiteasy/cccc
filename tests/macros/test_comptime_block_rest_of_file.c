// Ticket #292: bare #pragma jcc comptime (no 'begin' keyword) is sugar for
// #pragma jcc comptime begin — the block can still be closed with #pragma jcc end.

#pragma jcc comptime

int triple(int n) {
    return n * 3;
}

int base = 14;

[[jcc::comptime(inline)]]
$node_t *make_answer(void) {
    return $int_literal(triple($get_comptime_int("base")));
}

#pragma jcc end

int main(void) {
    if (make_answer() != 42)
        return 1;
    return 42;
}
