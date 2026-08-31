// Ticket #1248's original repro shape: a $@N list-splice chain spliced into
// a for-loop body, where the OUTER template is itself multi-statement (it
// declares `int i;` ahead of the loop, forcing quote_core's brace-auto-wrap
// path in src/reflection.c). The ticket suspected this combination itself
// was buggy; it is not -- called in statement position (as here), it has
// always worked. (The ticket's own repro additionally ended the template in
// `return i;` and called gen() in *expression* position -- that combination
// is a real bug, but an unrelated one: a comptime function returning a
// statement-kind node used in expression position, fixed and covered by
// test_macros_stmt_in_expr_position_error.c. A trailing `return i;` here,
// in statement position, would return from test() itself rather than being
// a red herring, so it's omitted -- this test isolates the $@N/brace-wrap
// shape specifically.)

[[cccc::comptime]]
Node *gen(Node *acc) {
    Node *s1    = Quote("$1 += 1;", acc);
    Node *chain = __builtin_node_list((Node *[]){s1}, 1);
    return Quote("int i; for (i = 0; i < 3; i++) { $@1; }", chain);
}

int test(void) {
    int acc = 0;
    gen(acc);
    return (acc == 3) ? 42 : 1;
}

int main(void) {
    return test();
}
