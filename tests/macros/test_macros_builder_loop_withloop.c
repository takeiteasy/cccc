// Ticket #1249: this is the ticket's literal repro
// (MakeWhile(cond, Quote("{ break; }"))), which no fix inside MakeWhile/
// MakeFor/MakeDoWhile alone can make compile -- C evaluates a function
// call's arguments before the call, so the eager Quote("{ break; }") is
// always parsed before MakeWhile ever runs and assigns the loop's labels.
// WithLoop(loop) { LoopSetBody(loop, Quote(...)); } instead builds the
// (bodyless) loop node first, pushes its labels, and only then parses the
// eager Quote() -- see man/MACROS.md, "Deferred templates with QuoteLazy".

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *loop = MakeWhile(MakeIntLiteral(1), NULL);
        WithLoop(loop) {
            LoopSetBody(loop, Quote("{ break; }"));
        }
        FunctionSetBody(fn, Quote("{ $1; return 0; }", loop));
    }
}
gen();

int main(void) {
    return f() == 0 ? 42 : 1;
}
