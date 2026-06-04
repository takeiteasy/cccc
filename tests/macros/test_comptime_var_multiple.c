// Ticket #188: multiple comptime variables of different types, declared in any
// order relative to macros that use them.

[[jcc::comptime]]
int base = 20;

[[jcc::macro(inline)]]
_Node *doubled(void) {
    return _AST_INT_LITERAL(_AST_GET_COMPTIME_INT("base") * 2);
}

[[jcc::comptime]]
int bonus = 2;

[[jcc::macro(inline)]]
_Node *total(void) {
    int64_t b = _AST_GET_COMPTIME_INT("base");
    int64_t n = _AST_GET_COMPTIME_INT("bonus");
    return _AST_INT_LITERAL(b + n);
}

int main(void) {
    if (doubled() != 40)
        return 1;
    if (total() != 22)
        return 2;
    return 42;
}
