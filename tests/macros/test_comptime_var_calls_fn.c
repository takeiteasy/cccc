// Ticket #191: comptime variable initializer calls a comptime function.
// buf_size = page_count() * 4096 cannot be evaluated as a constant expression
// by the C parser; it must be run in the macro VM after page_count is compiled.

[[jcc::comptime]]
int page_count(void) { return 4; }

[[jcc::comptime]]
int buf_size = page_count() * 4096;

[[jcc::comptime(inline)]]
$node_t *get_buf_size(void) {
    return $int_literal($get_comptime_int("buf_size"));
}

int main(void) {
    if (get_buf_size() != 16384)
        return 1;
    return 42;
}
