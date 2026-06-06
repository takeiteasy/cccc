// Ticket #292: #pragma jcc comptime begin...end makes enclosed functions
// implicitly [[jcc::comptime]] without requiring the attribute.

#pragma jcc comptime begin
int double_it(int n) {
    return n * 2;
}
#pragma jcc end

[[jcc::comptime(inline)]]
$node_t *make_doubled(void) {
    return $int_literal(double_it(21));
}

int main(void) {
    if (make_doubled() != 42)
        return 1;
    return 42;
}
