// Ticket #191: comptime variable initializer references another comptime
// variable (cross-var reference, evaluated in declaration order).

#pragma comptime
int compute_base(void) { return 7; }

#pragma comptime
int a = compute_base() * 3;   // a == 21

#pragma comptime
int b = a * 2;                 // b == 42  (references a)

#pragma macro
_Node *get_a(void) {
    return _AST_INT_LITERAL(_AST_GET_COMPTIME_INT("a"));
}

#pragma macro
_Node *get_b(void) {
    return _AST_INT_LITERAL(_AST_GET_COMPTIME_INT("b"));
}

int main(void) {
    if (get_a() != 21)
        return 1;
    if (get_b() != 42)
        return 2;
    return 42;
}
