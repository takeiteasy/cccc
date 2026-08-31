// Ticket #1247 follow-up: the same demand-driven index entry that lets
// Quote() see a function's *definition* (test_comptime_index_fn_definition_
// 1247.c) also feeds FindGlobal()/VarRef() -- both call
// cc_comptime_resolve_value_name, which searches the same CDK_PROTO
// registration. Before the fix, FindGlobal("bump10") returned NULL here
// because bump10 has no separate forward declaration, only a definition.

void bump10(void) {}

[[cccc::comptime]]
Node *gen(void) {
    Obj *fn = FindGlobal("bump10");
    return MakeIntLiteral(fn ? 42 : 1);
}
int check = gen();

int main(void) {
    return check;
}
