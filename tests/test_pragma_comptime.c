// Test pragma macros calling explicit compile-time helper functions.

#pragma comptime
int inc_int(int n) {
    return n + 1;
}

#pragma macro
_Node *make_answer(void) {
    return _AST_INT_LITERAL(inc_int(41));
}

#pragma macro
_Node *add_later_helper(_Node *x) {
    return _AST_BINARY(_ADD, x, _AST_INT_LITERAL(triple_later(2)));
}

#pragma comptime
int triple_later(int n) {
    return n * 3;
}

#pragma comptime
int mutual_even(int n) {
    if (n <= 0)
        return 0;
    return 1 + mutual_odd(n - 1);
}

#pragma comptime
int mutual_odd(int n) {
    if (n <= 0)
        return 0;
    return 1 + mutual_even(n - 1);
}

#pragma macro
_Node *mutual_add_four(_Node *x) {
    return _AST_BINARY(_ADD, x,
                          _AST_INT_LITERAL(mutual_even(4)));
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
