// Test pragma macros calling other pragma macros from compile-time bytecode.
// This intentionally omits <reflection.h>; pragma macro compilation includes it
// privately.

[[cccc::comptime]]
Node *forward_add_two(Node *x) {
    return add_two_later(x);
}

[[cccc::comptime]]
Node *add_one(Node *x) {
    return MakeBinary(NK_ADD, x, MakeIntLiteral(1));
}

[[cccc::comptime]]
Node *add_two_later(Node *x) {
    return add_one(add_one(x));
}

[[cccc::comptime]]
Node *chain_top(Node *x) {
    return add_one(add_two_later(x));
}

[[cccc::comptime]]
Node *mutual_even(int n, Node *x) {
    if (n <= 0)
        return x;
    return mutual_odd(n - 1, MakeBinary(NK_ADD, x, MakeIntLiteral(1)));
}

[[cccc::comptime]]
Node *mutual_odd(int n, Node *x) {
    if (n <= 0)
        return x;
    return mutual_even(n - 1, MakeBinary(NK_ADD, x, MakeIntLiteral(1)));
}

[[cccc::comptime]]
Node *mutual_add_four(Node *x) {
    return mutual_even(4, x);
}

int main(void) {
    if (forward_add_two(40) != 42)
        return 1;

    if (chain_top(10) != 13)
        return 2;

    if (mutual_add_four(38) != 42)
        return 3;

    return 42;
}
