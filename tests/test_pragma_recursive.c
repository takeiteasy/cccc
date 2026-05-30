// Test pragma macros calling other pragma macros from compile-time bytecode.
// This intentionally omits <reflection.h>; pragma macro compilation includes it
// privately.

#pragma macro
_Node *forward_add_two(_Node *x) {
    return add_two_later(x);
}

#pragma macro
_Node *add_one(_Node *x) {
    return _AST_BINARY(_ADD, x, _AST_INT_LITERAL(1));
}

#pragma macro
_Node *add_two_later(_Node *x) {
    return add_one(add_one(x));
}

#pragma macro
_Node *chain_top(_Node *x) {
    return add_one(add_two_later(x));
}

#pragma macro
_Node *mutual_even(int n, _Node *x) {
    if (n <= 0)
        return x;
    return mutual_odd(n - 1, _AST_BINARY(_ADD, x, _AST_INT_LITERAL(1)));
}

#pragma macro
_Node *mutual_odd(int n, _Node *x) {
    if (n <= 0)
        return x;
    return mutual_even(n - 1, _AST_BINARY(_ADD, x, _AST_INT_LITERAL(1)));
}

#pragma macro
_Node *mutual_add_four(_Node *x) {
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
