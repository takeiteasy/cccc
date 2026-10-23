// A break parsed under an outer loop and placed in an inner loop still leaves
// the outer loop, in the VM and in native output.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *hits  = MakeLocalVar("hits", GetType("int"));
        Node *outer = MakeWhile(Quote("$1 < 1000", hits), NULL);
        Node *inner = MakeWhile(MakeIntLiteral(1), NULL);
        Node *leave_outer = NULL;
        WithLoop(outer) {
            leave_outer = Quote("break;");
        }
        LoopSetBody(inner, Quote("{ $1 += 10; $2; }", hits, leave_outer));
        LoopSetBody(outer,
                    Quote("{ $1 += 1; $2; $1 += 100; }", hits, inner));
        FunctionSetBody(fn,
                        Quote("{ $1 = 0; $2; return $1; }", hits, outer));
    }
}
gen();

int main(void) {
    return f() == 11 ? 42 : 1;
}
