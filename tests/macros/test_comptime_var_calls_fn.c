// Ticket #191: comptime variable initializer calls a comptime function.
// buf_size = page_count() * 4096 cannot be evaluated as a constant expression
// by the C parser; it must be run in the macro VM after page_count is compiled.

[[cccc::comptime]]
int page_count(void) {
    return 4;
}

[[cccc::comptime]]
int buf_size = page_count() * 4096;

[[cccc::comptime]]
Node *get_buf_size(void) {
    return MakeIntLiteral(GetComptimeInt("buf_size"));
}

int main(void) {
    if (get_buf_size() != 16384)
        return 1;
    return 42;
}
