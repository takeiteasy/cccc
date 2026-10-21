// -c=native, a typedef whose right-hand side is an anonymous struct/union/enum
// (`typedef struct { ... } P, *Pp;`) used to serialize `typedef P *Pp;`
// referring to a `P` that was never printed at all when P itself is never used
// by value -- serialize_typedef_alias() deliberately skips an anonymous
// aggregate's own combined `typedef struct {...} P;` line, on the assumption
// serialize_struct_def() already printed the body while walking the
// usage-collected ctx->defs, which is empty here ('unknown type name P', a host
// compile failure). emit_typedef_and_deps() now emits the aggregate body itself
// when reached this way, gated by a shared emitted_defs dedup set so a comptime
// re-parse's duplicate TypeName record for the same declaration doesn't print
// it twice ('typedef redefinition with different types') -- which in turn
// needed same_type_or_origin() to gain a structural TY_ARRAY case, since two
// independently- parsed occurrences of an array member (e.g. `char n[8]`) never
// shared pointer identity. Asserts VM 42 -> native 42

typedef struct {
    int a[2];
} P, *Pp;
typedef struct {
    char n[8];
} A;
struct UsesA {
    A m;
};

[[cccc::comptime]]
int check(void) {
    Type *tp      = GetType("P");
    Type *tpp     = GetType("Pp");
    Type *ta      = GetType("A");
    Type *tuses_a = GetType("UsesA");
    if (tp && tpp && ta && tuses_a)
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
