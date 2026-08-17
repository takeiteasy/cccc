// Ticket #1034 (cluster B): a file-scope macro call whose returned ND_BLOCK
// gets spliced into the token stream for re-parse at global scope (#233,
// macros.c) used to ALSO drain its just-built Objs into macro_globals --
// leaving two copies of every generated function/type in the merged
// program. -c=native (and -c=generated) printed both: two prototypes, two
// bodies, "conflicting types for ..." from the host compiler. Separately,
// once that duplication was fixed, the surviving copy's struct tag (parsed
// from Quote()'s synthetic "<quote>" pseudo-file) was still wrongly
// suppressed as if a real #include supplied it (record_type_name()'s
// from_include check, parse_core.c, didn't recognize a synthetic file) --
// every use of the tag was then an incomplete-type error.
//
// Minimized from test_global_block_expansion.c.

[[cccc::comptime]]
Node *emit_counter_helpers(void) {
    return Quote("{ struct Counter { int n; }; void counter_init(struct Counter *c) { c->n = 0; } void counter_bump(struct Counter *c, int by) { c->n += by; } }");
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
