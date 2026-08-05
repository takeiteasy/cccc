// Ticket #907: a file-scope declaration's OWN tag is only the first
// struct|union|enum IDENT pair at depth 0 -- one nested inside a function
// declarator's parameter list must not be mistaken for it. Before the fix,
// the tag scan in index_declaration() (src/macros.c) matched the first such
// pair anywhere in the statement, so "struct S" inside a function pointer's
// or a prototype's parameter list hijacked decl_list_start and the
// declaration's real name (the typedef/object/prototype identifier) was
// never registered in the comptime declaration index at all -- any comptime
// body referencing it got a bogus "expected ','" / "undefined variable"
// parse error instead of a clean splice.
//
// Two shapes, both confirmed broken pre-fix:
//  - a function-pointer typedef whose parameter is "struct S *", used to
//    type a file-scope object a comptime body reads.
//  - a plain prototype "int f(struct S *);" named from a comptime body.
struct ParamTagS907 { int x; };

typedef int (*fp_paramtag_907)(struct ParamTagS907 *);
fp_paramtag_907 paramtag_fnptr_907;

int paramtag_proto_907(struct ParamTagS907 *);

[[cccc::comptime]]
int use_paramtag(void) {
    // Referencing fp_paramtag_907 (a typedef) forces the comptime
    // declaration index to resolve it -- previously impossible because
    // "struct ParamTagS907" inside its parameter list was mistaken for
    // the typedef statement's own tag, so the typedef itself was never
    // registered under its real name.
    (void)paramtag_fnptr_907;

    // paramtag_proto_907 is a bodiless prototype, never defined or called
    // anywhere -- FindGlobal() only needs its *name* to resolve through
    // the comptime declaration index (the same CDK_PROTO path broken by
    // the identical "struct ParamTagS907" parameter-list tag mixup), not
    // a real runtime address, so this doesn't require ever calling it.
    if (!FindGlobal("paramtag_proto_907"))
        return 0;
    return 42;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(use_paramtag())));
}
gen();

int main(void) {
    return result();
}
