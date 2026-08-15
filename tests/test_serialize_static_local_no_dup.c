// CCCC_FLAGS: -m
// CCCC_C4_SKIP: -m dumps source and exits, no bytecode to round-trip
// CCCC_EXPECT_STDOUT: static struct Foo1011 __cccc_a_0;
// CCCC_REJECT_STDOUT: __cccc_a_0;[\s]*static struct Foo1011 __cccc_a_0;
//
// #1011: a function-local `static` gets hoisted to a file-scope shadow
// global by rename_anon_globals() (src/serialize.c, e.g. `__cccc_a_0`
// here), and serialize_global_var() used to emit an identical declaration
// for it a second time, back to back, in both -m and -c=native output --
// the #918/#928 forward-declaration pass (cc_serialize_program) already
// prints `static struct Foo1011 __cccc_a_0;` ahead of every definition,
// and serialize_global_var() printed the byte-identical text again since
// the global has no initializer. Harmless (a redundant tentative
// definition is legal C) but unintentional. Fixed by skipping
// serialize_global_var()'s own line when the global has no init_data and
// is either static or a non-definition declaration -- both shapes the
// forward-declaration pass already fully supplies. Verified this exact
// program printed `static struct Foo1011 __cccc_a_0;` twice, consecutively,
// before the fix.
struct Foo1011 { int x; };
typedef struct Foo1011 Foo1011;

Foo1011 *make_1011(void) {
    static struct Foo1011 a;
    a.x = 42;
    return &a;
}

int get_x_1011(Foo1011 *t) { return t->x; }

int main(void) {
    Foo1011 *t = make_1011();
    return get_x_1011(t);
}
