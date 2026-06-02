// Ticket #188: #pragma comptime function declarations still work alongside
// comptime variable declarations — regression guard.

#pragma comptime
int double_it(int n) { return n * 2; }

#pragma comptime
int answer = 21;

#pragma macro
_Node *get_answer(void) {
    return _AST_INT_LITERAL(_AST_GET_COMPTIME_INT("answer") * 2);
}

#pragma macro
_Node *use_helper(_Node *x) {
    return _AST_BINARY(_ADD, x, _AST_INT_LITERAL(double_it(1)));
}

int main(void) {
    if (get_answer() != 42)
        return 1;
    // use_helper(40) => 40 + double_it(1) => 40 + 2 = 42
    if (use_helper(40) != 42)
        return 2;
    return 42;
}
