// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_REJECT_STDOUT: \.L\.\.
// CCCC_EXPECT_STDOUT: static struct FPt __cccc_
// CCCC_EXPECT_STDOUT: static int __cccc_
// Ticket #928: file-scope CompoundLiteral/InitArray/InitStruct anon globals
// (ticket #304, test_ast_builders_304.c) must also serialize cleanly under
// -c=generated, not just -m/-c=native. reflect_new_anon_gvar()'s `.L..N`
// name must be renamed to a real identifier and given a real definition
// even on the -c=generated emit-event path, and the definition must come
// before any macro-generated function that references it (the drain into
// macro_globals reverses creation order within one macro invocation, so
// the anon gvar here is created before the functions in gen_gvar_struct()
// -- forward-declaring it is what keeps the -c=generated output valid C).

struct FPt { int x; int y; };

// ---- CompoundLiteral outside WithFn (current_fn == NULL) ---------------
[[cccc::comptime]]
Node *gen_gvar_cl(void) {
    Type *pt_ty = GetType("FPt");
    Type *int_ty = GetType("int");

    Node *gpt = CompoundLiteral(pt_ty, MakeIntLiteral(7), MakeIntLiteral(13));

    Obj *fn = MakeFunction("gvar_cl_x", int_ty);
    WithFn(fn) {
        FunctionSetBody(fn, MakeReturn(MakeMember(gpt, "x")));
    }
    return MakeIntLiteral(0);
}
gen_gvar_cl();

// ---- InitArray outside WithFn -- also exercises the #928 array-cast fix -
[[cccc::comptime]]
Node *gen_gvar_arr(void) {
    Type *int_ty = GetType("int");

    Node *garr = InitArray(int_ty,
        MakeIntLiteral(10), MakeIntLiteral(20), MakeIntLiteral(30));

    Obj *fn = MakeFunction("gvar_arr_elem2", int_ty);
    WithFn(fn) {
        FunctionSetBody(fn, MakeReturn(MakeSubscript(garr, MakeIntLiteral(2))));
    }
    return MakeIntLiteral(0);
}
gen_gvar_arr();

// ---- InitStruct outside WithFn, partial (y should be zero) --------------
[[cccc::comptime]]
Node *gen_gvar_struct(void) {
    Type *pt_ty = GetType("FPt");
    Type *int_ty = GetType("int");

    const char *flds[] = {"x"};
    Node *vals[] = {MakeIntLiteral(99)};
    Node *gs = InitStruct(pt_ty, flds, vals, 1);

    Obj *fn_x = MakeFunction("gvar_struct_x", int_ty);
    WithFn(fn_x) {
        FunctionSetBody(fn_x, MakeReturn(MakeMember(gs, "x")));
    }

    Obj *fn_y = MakeFunction("gvar_struct_y", int_ty);
    WithFn(fn_y) {
        FunctionSetBody(fn_y, MakeReturn(MakeMember(gs, "y")));
    }
    return MakeIntLiteral(0);
}
gen_gvar_struct();
