// Ticket #188: [[cccc::comptime]] function declarations still work alongside
// comptime variable declarations — regression guard.

[[cccc::comptime]]
int double_it(int n) {
    return n * 2;
}

[[cccc::comptime]]
int answer = 21;

[[cccc::comptime]]
Node *get_answer(void) {
    return MakeIntLiteral(GetComptimeInt("answer") * 2);
}

[[cccc::comptime]]
Node *use_helper(Node *x) {
    return MakeBinary(NK_ADD, x, MakeIntLiteral(double_it(1)));
}

int main(void) {
    if (get_answer() != 42)
        return 1;
    // use_helper(40) => 40 + double_it(1) => 40 + 2 = 42
    if (use_helper(40) != 42)
        return 2;
    return 42;
}
