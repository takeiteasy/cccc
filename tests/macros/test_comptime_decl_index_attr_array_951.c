// Ticket #951 (attribute variant, same root cause and same fix as
// test_comptime_decl_index_anon_array_951.c): segment_declarator_name()
// (src/macros.c) also mis-scans a declaration carrying a leading C23
// attribute-specifier-seq ("[[deprecated]] typedef struct {...} B;"). Its
// two opening brackets are seen as an array-dimension start before any
// real declarator token has been walked, so `result` becomes NULL and the
// declaration is never indexed under its real name at all -- distinct from
// the anonymous-struct-array case, but the same missing-brace/bracket
// discipline in the same function. Left unfixed, a later use of "B" as a
// member type (as below) misparses with an unrelated "expected ','".
[[deprecated]] typedef struct {
    int y;
} B;
struct UsesB {
    B m;
};

[[cccc::comptime]]
int check(void) {
    Type *tb      = GetType("B");
    Type *tuses_b = GetType("UsesB");
    if (tb && tuses_b)
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
