// A comptime function returning an `asm(...)` statement via Quote() must
// splice in as a statement. node_is_stmt_kind() now classifies ND_ASM as a
// statement (matching gen_stmt() in src/codegen_stmt.c); quote_is_stmt()
// recognises the `asm` / `__asm__` / `__asm` spellings so a semicolon-less
// template still routes to statement parsing; and asm_stmt() consumes its
// trailing `;` so `Quote("asm(\"...\");")` leaves no stray tokens behind.
//
// With no asm callback or passthru configured, gen_stmt()'s ND_ASM arm is a
// no-op, so this runs on every target.

[[cccc::comptime]]
Node *nop_stmt(void) {
    return Quote("asm(\"nop\");");
}

[[cccc::comptime]]
Node *nop_stmt_no_semi(void) {
    return Quote("asm(\"nop\")");
}

int test(void) {
    nop_stmt();
    nop_stmt_no_semi();
    return 42;
}

int main(void) {
    return test();
}
