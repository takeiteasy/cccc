// Ticket #287: compile-time macros can receive fixed parameters beyond 8.

[[cccc::comptime]]
Node *ninth_arg(Node *a0, Node *a1, Node *a2, Node *a3,
                   Node *a4, Node *a5, Node *a6, Node *a7,
                   Node *a8) {
    return a8;
}

[[cccc::comptime]]
Node *fixed9_plus_tail(Node *a0, Node *a1, Node *a2,
                          Node *a3, Node *a4, Node *a5,
                          Node *a6, Node *a7, Node *a8, ...) {
    return MakeBinary(NK_ADD, a8, VarargAt(0));
}

int main(void) {
    int a = ninth_arg(1, 2, 3, 4, 5, 6, 7, 8, 9);
    int b = fixed9_plus_tail(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    return a == 9 && b == 19 ? 42 : 1;
}
