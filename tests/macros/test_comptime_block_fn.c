// Ticket #292: #pragma cccc comptime begin...end makes enclosed functions
// implicitly [[cccc::comptime]] without requiring the attribute.

#pragma cccc comptime begin
int double_it(int n) {
    return n * 2;
}
#pragma cccc comptime end

[[cccc::comptime]]
Node *make_doubled(void) {
    return MakeIntLiteral(double_it(21));
}

int main(void) {
    if (make_doubled() != 42)
        return 1;
    return 42;
}
