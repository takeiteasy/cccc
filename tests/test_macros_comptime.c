// Test pragma macros calling explicit compile-time helper functions.

[[cccc::comptime]]
int inc_int(int n) {
    return n + 1;
}

[[cccc::comptime(inline)]]
Node *make_answer(void) {
    return MakeIntLiteral(inc_int(41));
}

[[cccc::comptime(inline)]]
Node *add_later_helper(Node *x) {
    return MakeBinary(NK_ADD, x, MakeIntLiteral(triple_later(2)));
}

[[cccc::comptime]]
int triple_later(int n) {
    return n * 3;
}

[[cccc::comptime]]
int mutual_even(int n) {
    if (n <= 0)
        return 0;
    return 1 + mutual_odd(n - 1);
}

[[cccc::comptime]]
int mutual_odd(int n) {
    if (n <= 0)
        return 0;
    return 1 + mutual_even(n - 1);
}

[[cccc::comptime(inline)]]
Node *mutual_add_four(Node *x) {
    return MakeBinary(NK_ADD, x,
                          MakeIntLiteral(mutual_even(4)));
}

int main(void) {
    if (make_answer() != 42)
        return 1;

    if (add_later_helper(36) != 42)
        return 2;

    if (mutual_add_four(38) != 42)
        return 3;

    return 42;
}
