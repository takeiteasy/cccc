// CCCC_FLAGS: --testing

// CopyNode copies labels, switches, declarations and cleanup scopes; the suite
// also runs under -c=native, where copied labels and locals must stay distinct.

int  cp_g, cp_v, cp_cnt;
void cp_bump(int *p) {
    (void)p;
    cp_cnt++;
}

[[cccc::comptime]]
Node *cp_label(void) {
    Node *s    = Quote("{ goto out; cp_g += 100; out: cp_g += 21; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

[[cccc::comptime]]
Node *cp_label_value(void) {
    Node *s = Quote(
        "{ void *p = &&out; goto *p; cp_g += 100; out: cp_g += 21; }");
    Node *x[2] = {s, CopyNode(s)};
    return __builtin_ast_block(x, 2);
}

[[cccc::comptime]]
Node *cp_switch(void) {
    Node *s = Quote("switch (cp_v) { case 1: cp_g += 5; case 2 ... 4: cp_g += 1;"
                    " break; default: cp_g += 100; }");
    Node *x[3] = {s, CopyNode(s), CopyNode(s)};
    return __builtin_ast_block(x, 3);
}

[[cccc::comptime]]
Node *cp_decl(void) {
    Node *s    = Quote("{ int x; int y = 1; x = 9; cp_g += x + y; }");
    Node *x[3] = {s, CopyNode(s), Quote("{ int x = 22; cp_g += x; }")};
    return __builtin_ast_block(x, 3);
}

[[cccc::comptime]]
Node *cp_cleanup_scope(void) {
    Node *loop = Quote("for (;;) { cp_g += 15; break; }");
    Node *jump = Quote("{ goto out; cp_g += 100; out: cp_g += 1; }");
    Node *sw =
        Quote("switch (cp_v) { case 1: cp_g += 5; break; default: cp_g += 100; }");
    Node *s[6] = {loop, CopyNode(loop), jump, CopyNode(jump), sw, CopyNode(sw)};
    return Quote("{ __attribute__((cleanup(cp_bump))) int c = 0; $1; }",
                 __builtin_ast_block(s, 6));
}

[[cccc::test]]
void test_copy_label(void) {
    cp_g = 0;
    cp_label();
    AssertEq(cp_g, 42);
}

[[cccc::test]]
void test_copy_label_value(void) {
    cp_g = 0;
    cp_label_value();
    AssertEq(cp_g, 42);
}

[[cccc::test]]
void test_copy_switch(void) {
    cp_g = 0;
    cp_v = 1;
    cp_switch();
    AssertEq(cp_g, 18);
}

[[cccc::test]]
void test_copy_decl(void) {
    cp_g = 0;
    cp_decl();
    AssertEq(cp_g, 42);
}

[[cccc::test]]
void test_copy_cleanup_scope(void) {
    cp_g   = 0;
    cp_v   = 1;
    cp_cnt = 0;
    cp_cleanup_scope();
    AssertEq(cp_g, 42);
    AssertEq(cp_cnt, 1);
}
