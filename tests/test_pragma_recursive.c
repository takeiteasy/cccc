// Test pragma macros calling other pragma macros from compile-time bytecode.
// This intentionally omits <reflection.h>; pragma macro compilation includes it
// privately.

#pragma macro
JCC_Node *forward_add_two(JCC_Node *x) {
    return add_two_later(x);
}

#pragma macro
JCC_Node *add_one(JCC_Node *x) {
    return JCC_AST_BINARY(JCC_ND_ADD, x, JCC_AST_INT_LITERAL(1));
}

#pragma macro
JCC_Node *add_two_later(JCC_Node *x) {
    return add_one(add_one(x));
}

#pragma macro
JCC_Node *chain_top(JCC_Node *x) {
    return add_one(add_two_later(x));
}

#pragma macro
JCC_Node *mutual_even(int n, JCC_Node *x) {
    if (n <= 0)
        return x;
    return mutual_odd(n - 1, JCC_AST_BINARY(JCC_ND_ADD, x, JCC_AST_INT_LITERAL(1)));
}

#pragma macro
JCC_Node *mutual_odd(int n, JCC_Node *x) {
    if (n <= 0)
        return x;
    return mutual_even(n - 1, JCC_AST_BINARY(JCC_ND_ADD, x, JCC_AST_INT_LITERAL(1)));
}

#pragma macro
JCC_Node *mutual_add_four(JCC_Node *x) {
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
