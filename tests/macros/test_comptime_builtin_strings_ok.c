// NULL strings, literals in templates and ordinary tails are accepted.
[[cccc::comptime]]
Node *probe(void) {
    int64_t     v        = 0;
    const char *names[1] = {"x"};
    if (FindType(NULL) != NULL || __builtin_ast_const_int_value(NULL, NULL))
        return MakeIntLiteral(1);
    if (__builtin_ast_offsetof_chain(GetType("int"), names, 1) != -1)
        return MakeIntLiteral(2);
    EmitDirective(NULL);
    // "$5" and $7 inside a literal or a comment are not placeholders.
    Node *s = Quote("sizeof(\"$5\") /* $7 */ + $$ + '$'", MakeIntLiteral(0));
    if (!s || !__builtin_ast_const_int_value(MakeIntLiteral(42), &v) || v != 42)
        return MakeIntLiteral(3);
    return Quote("$1 + $2", MakeIntLiteral(40), MakeIntLiteral(2));
}

int main(void) {
    return probe();
}
