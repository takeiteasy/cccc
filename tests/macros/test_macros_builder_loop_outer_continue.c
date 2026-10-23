// A continue parsed under an outer loop and placed in an inner loop still
// continues the outer loop, including past a variable length array declared
// in the outer body.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *hits   = MakeLocalVar("hits", GetType("int"));
        Node *rounds = MakeLocalVar("rounds", GetType("int"));
        Node *outer  = MakeWhile(Quote("$1 < 3", rounds), NULL);
        Node *inner  = MakeWhile(Quote("$1 < 1000", hits), NULL);
        Node *next_round = NULL;
        WithLoop(outer) {
            next_round = Quote("continue;");
        }
        LoopSetBody(inner, Quote("{ $1 += 10; $2; }", hits, next_round));
        LoopSetBody(outer,
                    Quote("{ $1++; $2; int v[$1]; $3 += 100; }", rounds,
                          inner, hits));
        FunctionSetBody(fn, Quote("{ $1 = 0; $2 = 0; $3; return $1; }", hits,
                                  rounds, outer));
    }
}
gen();

int main(void) {
    return f() == 30 ? 42 : 1;
}
