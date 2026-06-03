// Test finite nested macro expansion under the default recursion limit.

[[jcc::macro]]
_Node *finish_step(_Node *x) {
    return _AST_BINARY(_ADD, x, _AST_INT_LITERAL(1));
}

[[jcc::macro]]
_Node *start_step(_Node *x) {
    return _QUOTE("finish_step($1)", x);
}

int main(void) {
    return start_step(41);
}
