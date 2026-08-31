// Ticket #1247: a file-scope-called comptime function's Quote() template
// could not resolve a runtime-TU function that is only ever *defined*
// (no separate forward declaration) -- the #894 demand-driven declaration
// index skipped a function definition's whole extent, signature included,
// treating it the same as its body. A plain prototype ("void bump10(void);")
// already indexed fine as CDK_PROTO; only the definition shape was missing.
//
// This is a file-scope call ("gen();"), not a call from inside main() --
// the gap only shows up before the runtime TU itself has been parsed, when
// every name a comptime body references must come through the index.

int gx = 1;

void bump10(void) { gx += 10; }

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("{ bump10(); return 7; }"));
    }
    PublishNode(fn);
}
gen();

int f(void);

int main(void) {
    int r = f();
    return (r == 7 && gx == 11) ? 42 : 1;
}
