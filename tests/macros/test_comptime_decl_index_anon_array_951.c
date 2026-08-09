// Ticket #951: segment_declarator_name() (src/macros.c) locates a
// declaration's declared name by scanning forward for the first depth-0
// '[' array-dimension group. Without brace tracking, an array member
// inside an anonymous struct/union body declared in the same statement as
// its own declarator -- e.g. "typedef struct { char n[32]; } A;" -- is
// misread: the member's '[' looks like a depth-0 array dimension, so the
// typedef is indexed under the member's name ("n") instead of its own
// ("A"). Anything that later needs "A" as a typename (e.g. using it as
// another struct's member type) fails to resolve it and misparses with an
// unrelated "expected ','".
//
// This also covers a second declarator segment in the same declaration
// ("P, *Pp") to confirm both names are indexed correctly once brace
// depth is tracked, and the multi-word bug title ("union" in the ticket)
// against the struct form, which fails identically.

typedef struct { char n[32]; } A;
struct UsesA { A m; };

typedef struct { int a[2]; } P, *Pp;
struct UsesP { P m; Pp p; };

union UsesAUnion { A m; int x; };

[[cccc::comptime]]
int check(void) {
    Type *ta = GetType("A");
    Type *tuses_a = GetType("UsesA");
    Type *tp = GetType("P");
    Type *tpp = GetType("Pp");
    Type *tuses_p = GetType("UsesP");
    Type *tuses_union = GetType("UsesAUnion");
    if (ta && tuses_a && tp && tpp && tuses_p && tuses_union)
        return 42;
    return 0;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(check())));
}
gen();

int main(void) {
    return result();
}
