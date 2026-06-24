// Test ticket #284: variadic comptime macros receive an unbounded AST tail.

[[cccc::comptime]]
Node *sum_nodes(...) {
    int count = VarargCount();
    if (count == 0)
        return MakeIntLiteral(0);

    Node *acc = VarargAt(0);
    for (int i = 1; i < count; i = i + 1)
        acc = MakeBinary(NK_ADD, acc, VarargAt(i));
    return acc;
}

[[cccc::comptime]]
Node *add_tail(Node *base, ...) {
    Node *acc = base;
    for (int i = 0; i < VarargCount(); i = i + 1)
        acc = MakeBinary(NK_ADD, acc, VarargAt(i));
    return acc;
}

int main(void) {
    if (sum_nodes(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55)
        return 1;
    if (add_tail(10, 1, 2, 3, 4, 5, 6, 7, 8, 9) != 55)
        return 2;
    return 42;
}
