// buffalo tracker #18: a comptime-generated function whose signature names
// a tagless `typedef struct { ... } T;` produced an incomplete type under
// -c=native/-c=generated.
//
// synthesize_forward_decl_tokens() (src/macros.c) prepends a forward
// declaration of every generated function to the head of each input token
// stream, so the parser can resolve calls to it that appear earlier in the
// file. That text has always spelled an aggregate return/param type as
// `struct %s` using Type.name -- correct for a genuinely tagged struct, but
// for a tagless typedef Type.name is the *typedef* name (declarator()
// overwrites it with whatever last named the object, parse_types.c), not a
// tag. The forward declaration therefore read `struct Pair wrap(void);`
// (`struct BufLexer *` in the ticket's own wording) -- a reference to a tag
// that was never declared. Reparsing it minted a brand new, forever-
// incomplete `struct Pair`, unrelated to the real (complete) anonymous
// struct; every downstream site typed from wrap()'s declared signature
// (notably the caller's compiler-synthesized struct-return temp) was then
// left incomplete -- "variable has incomplete type 'struct Pair'" under
// -c=native, matching the ticket's own error text exactly.
//
// Fixed by install_tag_alias_for_reparse() (src/macros.c): before spelling
// the forward declaration, it installs the tagless Type as a scope-only tag
// alias (a TagScopeNode, matched by name content on reparse -- exactly
// find_tag()'s own lookup, parse_core.c) WITHOUT setting Type.struct_tag or
// recording a TypeNameRecord. The reparsed reference then resolves to the
// same (already complete) Type object instead of minting a new one, while
// the serializer's tag machinery (which only ever sees TypeNameRecord
// entries) is entirely unaware anything happened -- TY_STRUCT still falls
// through to the typedef alias, exactly like the never-broken
// no-reflection case.
//
// This exercises both the return-type half (a tagless struct return) and
// the by-pointer param half the ticket also names (a second, distinct
// tagless struct passed as `T *`) -- the two mirror buffalo's
// `BufToken buf_next(BufLexer *lx)`. main() checks round-tripped values,
// not just that the program compiles.

typedef struct {
    int a;
    int b;
} Pair;

typedef struct {
    int scale;
} Ctx;

Pair mk_pair(Ctx *c);

Pair mk_pair(Ctx *c) {
    Pair p;
    p.a = c->scale;
    p.b = c->scale * 2;
    return p;
}

[[cccc::comptime]]
void generate_anon_typedef_wrapper_18(void) {
    Obj *fn = MakeFunction("wrap_pair_18", GetType("Pair"));
    FunctionAddParam(fn, "c", MakePointer(GetType("Ctx")));
    WithFn(fn) {
        FunctionSetBody(fn,
                        Quote("return mk_pair($1);", MakeParamRef(fn, "c")));
    }
}

generate_anon_typedef_wrapper_18();

int main(void) {
    Ctx  c = {20};
    Pair p = wrap_pair_18(&c);
    if (p.a != 20 || p.b != 40)
        return 1;

    return 42;
}
