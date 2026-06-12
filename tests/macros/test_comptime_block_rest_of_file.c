// Bare #pragma cccc comptime (no 'begin') marks everything from the pragma to
// EOF as comptime; it runs to the end of the file without a closing 'end'.

#pragma cccc comptime begin

int triple(int n) {
    return n * 3;
}

int base = 14;

[[cccc::comptime(inline)]]
$node_t *make_answer(void) {
    return $int_literal(triple($get_comptime_int("base")));
}

#pragma cccc comptime end

int main(void) {
    if (make_answer() != 42)
        return 1;
    return 42;
}
