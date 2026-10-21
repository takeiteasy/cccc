// -c=native, a file-scope macro call whose returned ND_BLOCK is spliced into
// the token stream for re-parse at global scope used to ALSO drain its
// just-built Objs into macro_globals -- two copies of every generated function
// reaching the merged program, so -c=native printed two prototypes/bodies per
// function ('conflicting types'). Fixed by discarding the drain when the splice
// path is taken. Separately, the surviving struct tag (parsed from Quote()'s
// synthetic "<quote>" pseudo-file) was still wrongly treated as from_include
// (record_type_name(), parse_core.c) and its definition suppressed -- fixed by
// recognizing exactly the "<quote>" pseudo-file as never from_include, distinct
// from tokenize_private_header()'s own "<...>" real-header tags. Asserts VM 42
// -> native 42

[[cccc::comptime]]
Node *emit_counter_helpers(void) {
    return Quote("{ struct Counter { int n; }; void counter_init(struct "
                 "Counter *c) { c->n = 0; } void counter_bump(struct Counter "
                 "*c, int by) { c->n += by; } }");
}

emit_counter_helpers();

int main(void) {
    struct Counter c;
    counter_init(&c);
    if (c.n != 0)
        return 1;
    counter_bump(&c, 5);
    counter_bump(&c, 37);
    if (c.n != 42)
        return 2;
    return 42;
}
